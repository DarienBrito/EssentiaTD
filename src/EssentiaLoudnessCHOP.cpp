// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaLoudnessCHOP.h"

#include <cmath>
#include <algorithm>
#include <numeric>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double kMomentaryWindowSec = 0.400; // EBU R128 momentary: 400 ms
static constexpr double kShortTermWindowSec = 3.000; // EBU R128 short-term: 3 s
static constexpr float  kAbsoluteGateLufs  = -70.0f; // EBU R128 absolute gate
static constexpr float  kAbsGatePower      = 1e-7f;  // 10^(kAbsoluteGateLufs/10)
static constexpr double kRelGatePowerRatio = 0.1;    // -10 LU relative gate in power
static constexpr float  kSilenceLufs       = -144.0f; // floor for empty windows
static constexpr float  kLufsOffset        = -0.691f; // ITU-R BS.1770 mono offset

// ---------------------------------------------------------------------------
// ITU-R BS.1770-4 K-weighting coefficient computation
//
// Stage 1: high-shelf filter modeling head transfer function
// Stage 2: high-pass filter (RLB weighting, ~38 Hz cutoff)
//
// Analog prototype parameters from ITU-R BS.1770-4, digital coefficients
// computed via bilinear transform for any sample rate.
// ---------------------------------------------------------------------------

void computeKWeightShelf(double fs, Biquad& bq)
{
	const double db = 3.999843853973347;
	const double f0 = 1681.974450955533;
	const double Q  = 0.7071752369554196;

	const double K  = std::tan(M_PI * f0 / fs);
	const double Vh = std::pow(10.0, db / 20.0);
	const double Vb = std::pow(Vh, 0.4996667741545416);
	const double a0 = 1.0 + K / Q + K * K;

	bq.b0 = (Vh + Vb * K / Q + K * K) / a0;
	bq.b1 = 2.0 * (K * K - Vh) / a0;
	bq.b2 = (Vh - Vb * K / Q + K * K) / a0;
	bq.a1 = 2.0 * (K * K - 1.0) / a0;
	bq.a2 = (1.0 - K / Q + K * K) / a0;
	bq.reset();
}

void computeKWeightHighpass(double fs, Biquad& bq)
{
	const double f0 = 38.13547087602444;
	const double Q  = 0.5003270373238773;

	const double K  = std::tan(M_PI * f0 / fs);
	const double a0 = 1.0 + K / Q + K * K;

	bq.b0 =  1.0 / a0;
	bq.b1 = -2.0 / a0;
	bq.b2 =  1.0 / a0;
	bq.a1 = 2.0 * (K * K - 1.0) / a0;
	bq.a2 = (1.0 - K / Q + K * K) / a0;
	bq.reset();
}

// ---------------------------------------------------------------------------
// CRTP hook: getOutputInfoImpl
// ---------------------------------------------------------------------------

bool EssentiaLoudnessCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                              const OP_Inputs* inputs,
                                              bool isBatch)
{
	if (isBatch)
	{
		info->numChannels = kNumChannels;
		info->numSamples  = myHasResults ? myCachedNumFrames : 1;
		info->startIndex  = 0;
		info->sampleRate  = myHasResults ? myCachedSampleRate : 1.0f;
	}
	else
	{
		info->numChannels = kNumChannels;
		info->numSamples  = 1;
		info->sampleRate = static_cast<float>(sanitizedCookRate(inputs));
	}

	return true;
}

// ---------------------------------------------------------------------------
// CRTP hook: getChannelNameImpl
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::getChannelNameImpl(int32_t index,
                                               OP_String* name,
                                               const OP_Inputs*)
{
	switch (index)
	{
	case 0: name->setString("loudness");            break;
	case 1: name->setString("loudness_momentary");  break;
	case 2: name->setString("loudness_shortterm");  break;
	case 3: name->setString("loudness_integrated"); break;
	case 4: name->setString("dynamic_range");       break;
	case 5: name->setString("rms");                 break;
	case 6: name->setString("zcr");                 break;
	default: name->setString("unknown");            break;
	}
}

// ---------------------------------------------------------------------------
// CRTP hook: executeRealtimeImpl
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::executeRealtimeImpl(CHOP_Output* output,
                                                const OP_Inputs* inputs)
{
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		zeroOutput(output);
		return;
	}

	// Read parameters
	const int   frameSize    = ParametersLoudness::evalFramesize(inputs);
	const float zcrThreshold = ParametersLoudness::evalZcrthreshold(inputs);

	// Validate input
	const OP_CHOPInput* audioIn = inputs->getInputCHOP(0);
	if (!audioIn || audioIn->numChannels < 1 || audioIn->numSamples < 1)
	{
		myError = "No audio input connected";
		zeroOutput(output);
		return;
	}

	double sampleRate = audioIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	// Reconfigure when frame size, sample rate, or ZCR threshold changes
	if (frameSize != myFrameSize || sampleRate != mySampleRate
	    || zcrThreshold != myZcrThreshold)
	{
		try
		{
			configureAlgorithms(frameSize, sampleRate, zcrThreshold);

			myFrameSize    = frameSize;
			mySampleRate   = sampleRate;
			myZcrThreshold = zcrThreshold;

			// Ring buffer: hold at least 4 frames worth of samples
			myAudioRing.resize(static_cast<size_t>(frameSize) * 4);
			myHopCounter = 0;

			myAudioFrame.resize(static_cast<size_t>(frameSize), 0.0f);

			// Compute window capacities in frames
			myMomentaryCapacity = std::max(1, static_cast<int>(std::ceil(
				kMomentaryWindowSec * sampleRate / static_cast<double>(frameSize))));

			myShortTermCapacity = std::max(1, static_cast<int>(std::ceil(
				kShortTermWindowSec * sampleRate / static_cast<double>(frameSize))));

			// Clear all windowed state
			myMomentaryWindow.clear();
			myShortTermWindow.clear();
			myIntegratedPowers.clear();
			myAbsGatedPowerSum = 0.0;
			myAbsGatedCount    = 0;
			myIntegratedRecomputeCounter = 0;

			// Reset output values
			myLoudnessLufs         = kSilenceLufs;
			myMomentaryLoudness    = kSilenceLufs;
			myShortTermLoudness    = kSilenceLufs;
			myIntegratedLoudness   = kSilenceLufs;
			myDynamicRange         = 0.0f;
			myLufsMin              = -kSilenceLufs;
			myLufsMax              = kSilenceLufs;
		}
		catch (const std::exception& e)
		{
			myError = std::string("Algorithm config failed: ") + e.what();
			releaseAlgorithms();
		}
		catch (...)
		{
			myError = "Algorithm config failed with unknown error";
			releaseAlgorithms();
		}
	}

	// Accumulate input samples into ring buffer
	const float* audioData      = audioIn->getChannelData(0);
	const int    numInputSamples = audioIn->numSamples;

	for (int s = 0; s < numInputSamples; ++s)
	{
		myAudioRing.write(&audioData[s], 1);
		++myHopCounter;

		// Dispatch a new analysis frame every frameSize samples
		if (myHopCounter >= myFrameSize
		    && myAudioRing.available() >= static_cast<size_t>(myFrameSize))
		{
			processFrame();
			recomputeIntegrated();
			myHopCounter = 0;
		}
	}

	// Write output — repeat latest values for the whole time slice
	for (int s = 0; s < output->numSamples; ++s)
	{
		output->channels[0][s] = myLoudnessLufs;
		output->channels[1][s] = myMomentaryLoudness;
		output->channels[2][s] = myShortTermLoudness;
		output->channels[3][s] = myIntegratedLoudness;
		output->channels[4][s] = myDynamicRange;
		output->channels[5][s] = myRms;
		output->channels[6][s] = myZcr;
	}
}

// ---------------------------------------------------------------------------
// CRTP hook: snapshotAndLaunch
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::snapshotAndLaunch(AudioSnapshot audio,
                                              const OP_Inputs* inputs)
{
	BatchLoudnessParams params;
	params.frameSize    = ParametersLoudness::evalFramesize(inputs);
	params.zcrThreshold = ParametersLoudness::evalZcrthreshold(inputs);

	myRunner.launch([snap = std::move(audio), p = params]
		(const std::atomic<bool>& cancelFlag, std::atomic<float>& progress)
	{
		return computeBatchAsync(snap, p, cancelFlag, progress);
	});
}

// ---------------------------------------------------------------------------
// Override: writeOutputBatch — applies live normalization to cached results
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::writeOutputBatch(CHOP_Output* output,
                                             const OP_Inputs* /*inputs*/)
{
	if (myHasResults)
	{
		const int numCh   = std::min(output->numChannels,
		                             static_cast<int>(myResultCache.size()));
		const int numSamp = output->numSamples;

		for (int c = 0; c < numCh; ++c)
		{
			const auto& chData = myResultCache[c];
			for (int s = 0; s < numSamp; ++s)
				output->channels[c][s] = (s < static_cast<int>(chData.size()))
				                         ? chData[s] : 0.0f;
		}
		for (int c = numCh; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
	}
	else
	{
		zeroOutput(output);
	}
}

// ---------------------------------------------------------------------------
// CRTP hook: setupParametersImpl
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::setupParametersImpl(OP_ParameterManager* manager)
{
	setupModeParam(manager);
	setupBatchParams(manager);
	ParametersLoudness::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP
// ---------------------------------------------------------------------------

int32_t EssentiaLoudnessCHOP::getNumInfoCHOPChansImpl()
{
	return 7;
}

void EssentiaLoudnessCHOP::getInfoCHOPChanImpl(int32_t index,
                                                OP_InfoCHOPChan* chan)
{
	switch (index)
	{
	case 0:
		chan->name->setString("frame_size");
		chan->value = static_cast<float>(myFrameSize);
		break;
	case 1:
		chan->name->setString("sample_rate");
		chan->value = static_cast<float>(mySampleRate);
		break;
	case 2:
		chan->name->setString("num_frames");
		chan->value = static_cast<float>(myCachedNumFrames);
		break;
	case 3:
		chan->name->setString("computing");
		chan->value = myRunner.isComputing() ? 1.0f : 0.0f;
		break;
	case 4:
		chan->name->setString("progress");
		chan->value = myRunner.progress();
		break;
	case 5:
		chan->name->setString("lufs_min");
		chan->value = myLufsMin;
		break;
	case 6:
		chan->name->setString("lufs_max");
		chan->value = myLufsMax;
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Algorithm management
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::configureAlgorithms(int /*frameSize*/,
                                                double sampleRate,
                                                float zcrThreshold)
{
	releaseAlgorithms();

	// K-weighting filter coefficients (ITU-R BS.1770-4)
	computeKWeightShelf(sampleRate, myKWeightStage1);
	computeKWeightHighpass(sampleRate, myKWeightStage2);

	myZcrAlgo = AlgorithmFactory::create("ZeroCrossingRate",
		"threshold", static_cast<Real>(zcrThreshold));
}

void EssentiaLoudnessCHOP::releaseAlgorithms()
{
	delete myZcrAlgo;
	myZcrAlgo = nullptr;
}

// ---------------------------------------------------------------------------
// Per-frame processing (real-time)
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::processFrame()
{
	// Read the latest frameSize samples from the ring buffer
	myAudioRing.readLatest(myAudioFrame, static_cast<size_t>(myFrameSize));

	// Compute RMS from raw (un-weighted) audio frame
	{
		double sum = 0.0;
		for (int i = 0; i < myFrameSize; ++i)
			sum += static_cast<double>(myAudioFrame[i])
			     * static_cast<double>(myAudioFrame[i]);
		myRms = static_cast<float>(std::sqrt(sum / myFrameSize));
	}

	// Compute ZCR via Essentia's ZeroCrossingRate algorithm
	if (myZcrAlgo)
	{
		try {
			myZcrAlgo->input("signal").set(myAudioFrame);
			myZcrAlgo->output("zeroCrossingRate").set(myEssentiaZcr);
			myZcrAlgo->compute();
			myZcr = myEssentiaZcr;
		} catch (...) { myZcr = 0.0f; }
	}

	// K-weight the frame and compute mean-square power.
	// Filter state persists between frames for continuous filtering.
	double sumSquared = 0.0;
	for (int i = 0; i < myFrameSize; ++i)
	{
		float kw = myKWeightStage1.process(myAudioFrame[i]);
		kw = myKWeightStage2.process(kw);
		sumSquared += static_cast<double>(kw) * static_cast<double>(kw);
	}
	const double meanSquare = sumSquared / static_cast<double>(myFrameSize);

	// Convert to LUFS (ITU-R BS.1770: -0.691 + 10*log10(power))
	myLoudnessLufs = kLufsOffset + 10.0f * std::log10(
		static_cast<float>(meanSquare) + 1e-10f);

	// Track running min/max for Info CHOP
	if (myLoudnessLufs > myLufsMax) myLufsMax = myLoudnessLufs;
	if (myLoudnessLufs < myLufsMin) myLufsMin = myLoudnessLufs;

	// Push into momentary window
	myMomentaryWindow.push_back(myLoudnessLufs);
	if (static_cast<int>(myMomentaryWindow.size()) > myMomentaryCapacity)
		myMomentaryWindow.pop_front();

	// Push into short-term window
	myShortTermWindow.push_back(myLoudnessLufs);
	if (static_cast<int>(myShortTermWindow.size()) > myShortTermCapacity)
		myShortTermWindow.pop_front();

	// Update windowed loudness outputs
	myMomentaryLoudness = windowedLufs(myMomentaryWindow);
	myShortTermLoudness = windowedLufs(myShortTermWindow);

	// Accumulate the 400 ms MOMENTARY block for integrated loudness (EBU
	// R128 gates momentary blocks; gating 3 s short-term values biases the
	// result vs reference meters). Stored as power; cap at ~1 hour.
	static constexpr size_t kMaxIntegratedEntries = 200000;
	if (myIntegratedPowers.size() >= kMaxIntegratedEntries)
	{
		myIntegratedPowers.erase(myIntegratedPowers.begin(),
		                         myIntegratedPowers.begin()
		                         + static_cast<ptrdiff_t>(myIntegratedPowers.size() / 4));
		// Rebuild the incremental pass-1 sums after the trim
		myAbsGatedPowerSum = 0.0;
		myAbsGatedCount    = 0;
		for (const float p : myIntegratedPowers)
			if (p >= kAbsGatePower) { myAbsGatedPowerSum += p; ++myAbsGatedCount; }
	}
	const float blockPower =
		std::pow(10.0f, myMomentaryLoudness / 10.0f);
	myIntegratedPowers.push_back(blockPower);
	if (blockPower >= kAbsGatePower)
	{
		myAbsGatedPowerSum += blockPower;
		++myAbsGatedCount;
	}

	// Dynamic range: max - min of the short-term window
	if (!myShortTermWindow.empty())
	{
		const auto [minIt, maxIt] =
			std::minmax_element(myShortTermWindow.cbegin(), myShortTermWindow.cend());
		myDynamicRange = *maxIt - *minIt;
	}
	else
	{
		myDynamicRange = 0.0f;
	}
}

// ---------------------------------------------------------------------------
// Static helper: power-average of a deque of LUFS values
//
// The -0.691 offset cancels in the round-trip (LUFS→power→average→LUFS),
// so we use the simplified form: 10^(LUFS/10) and 10*log10(mean).
// ---------------------------------------------------------------------------

float EssentiaLoudnessCHOP::windowedLufs(const std::deque<float>& window)
{
	if (window.empty())
		return kSilenceLufs;

	double sumPower = 0.0;
	for (const float lufs : window)
		sumPower += std::pow(10.0, static_cast<double>(lufs) / 10.0);

	const double meanPower = sumPower / static_cast<double>(window.size());
	return static_cast<float>(10.0 * std::log10(meanPower + 1e-30));
}

// ---------------------------------------------------------------------------
// EBU R128 integrated loudness (two-pass gating, -70 LUFS absolute gate)
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::recomputeIntegrated()
{
	// Throttle: pass 2 scans every stored block (up to 200k) — running it
	// per dispatched frame is O(N²) over a session. ~5 Hz is plenty for a
	// value defined over the whole programme.
	if (++myIntegratedRecomputeCounter < 8)
		return;
	myIntegratedRecomputeCounter = 0;

	// Pass 1 (absolute gate, -70 LUFS) is maintained incrementally
	if (myAbsGatedCount == 0)
	{
		myIntegratedLoudness = kSilenceLufs;
		return;
	}
	const double ungatedMeanPower =
		myAbsGatedPowerSum / static_cast<double>(myAbsGatedCount);

	// Pass 2: relative gate at -10 LU below the ungated mean (power ratio)
	const double relGatePower = ungatedMeanPower * kRelGatePowerRatio;

	double sumPower2 = 0.0;
	int    count2    = 0;
	for (const float p : myIntegratedPowers)
	{
		if (p >= kAbsGatePower && p >= relGatePower)
		{
			sumPower2 += p;
			++count2;
		}
	}

	if (count2 == 0)
	{
		myIntegratedLoudness = kSilenceLufs;
		return;
	}

	myIntegratedLoudness = static_cast<float>(
		10.0 * std::log10(sumPower2 / static_cast<double>(count2) + 1e-30));
}

// ===========================================================================
// Static async worker — no `this`, creates local state
// ===========================================================================

AsyncBatchResult EssentiaLoudnessCHOP::computeBatchAsync(
	const AudioSnapshot&       audio,
	const BatchLoudnessParams& params,
	const std::atomic<bool>&   cancelFlag,
	std::atomic<float>&        progress)
{
	AsyncBatchResult result;
	result.success = false;

	const int   frameSize = params.frameSize;

	double sampleRate = audio.sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	const int audioLength = audio.numSamples;
	const int numFrames   = (frameSize > 0) ? (audioLength / frameSize) : 0;

	if (numFrames == 0)
	{
		result.warning = "Audio too short for given frame size";
		result.success = true; // not a hard error — return empty
		return result;
	}

	// Create local K-weighting filters
	Biquad stage1, stage2;
	computeKWeightShelf(sampleRate, stage1);
	computeKWeightHighpass(sampleRate, stage2);

	// Create local ZCR algorithm
	Algorithm* zcrAlgo = AlgorithmFactory::create("ZeroCrossingRate",
		"threshold", static_cast<Real>(params.zcrThreshold));

	// Window capacities (in frames)
	const int momentaryCap = std::max(1, static_cast<int>(std::ceil(
		kMomentaryWindowSec * sampleRate / static_cast<double>(frameSize))));
	const int shortTermCap = std::max(1, static_cast<int>(std::ceil(
		kShortTermWindowSec * sampleRate / static_cast<double>(frameSize))));

	// Allocate result cache: kNumChannels x numFrames
	result.cache.assign(EssentiaLoudnessCHOP::kNumChannels,
	                    std::vector<float>(numFrames, 0.0f));

	// Processing state
	std::deque<float>  momentaryWindow;
	std::deque<float>  shortTermWindow;
	std::vector<float> integratedPowers;   // gated 400 ms momentary blocks
	integratedPowers.reserve(static_cast<size_t>(numFrames));
	double absGatedPowerSum = 0.0;         // incremental pass-1 sum
	int    absGatedCount    = 0;

	std::vector<Real> frame(static_cast<size_t>(frameSize), 0.0f);
	Real essentiaZcr = 0.0f;

	const float* audioData = audio.data.data();

	for (int f = 0; f < numFrames; ++f)
	{
		if (cancelFlag.load(std::memory_order_relaxed))
		{
			delete zcrAlgo;
			result.success = false;
			result.error   = "Cancelled";
			return result;
		}

		progress.store(static_cast<float>(f) / static_cast<float>(numFrames),
		               std::memory_order_relaxed);

		const int offset = f * frameSize;

		// Copy frame
		for (int i = 0; i < frameSize; ++i)
			frame[static_cast<size_t>(i)] =
				static_cast<Real>(audioData[offset + i]);

		// K-weight the frame and compute mean-square power
		double sumSquared = 0.0;
		for (int i = 0; i < frameSize; ++i)
		{
			float kw = stage1.process(static_cast<float>(frame[static_cast<size_t>(i)]));
			kw = stage2.process(kw);
			sumSquared += static_cast<double>(kw) * static_cast<double>(kw);
		}
		const double meanSquare = sumSquared / static_cast<double>(frameSize);

		// Per-frame LUFS
		float loudnessLufs = kLufsOffset + 10.0f * std::log10(
			static_cast<float>(meanSquare) + 1e-10f);

		// RMS (from raw audio, not K-weighted)
		double sumRms = 0.0;
		for (int i = 0; i < frameSize; ++i)
			sumRms += static_cast<double>(frame[static_cast<size_t>(i)])
			        * static_cast<double>(frame[static_cast<size_t>(i)]);
		float rms = static_cast<float>(std::sqrt(sumRms / static_cast<double>(frameSize)));

		// ZCR
		float zcr = 0.0f;
		try {
			zcrAlgo->input("signal").set(frame);
			zcrAlgo->output("zeroCrossingRate").set(essentiaZcr);
			zcrAlgo->compute();
			zcr = static_cast<float>(essentiaZcr);
		} catch (...) {}

		// Momentary window
		momentaryWindow.push_back(loudnessLufs);
		if (static_cast<int>(momentaryWindow.size()) > momentaryCap)
			momentaryWindow.pop_front();

		// Short-term window
		shortTermWindow.push_back(loudnessLufs);
		if (static_cast<int>(shortTermWindow.size()) > shortTermCap)
			shortTermWindow.pop_front();

		// Momentary LUFS (power-average over 400 ms window)
		float momentaryLufs = [&]() -> float {
			if (momentaryWindow.empty()) return kSilenceLufs;
			double sp = 0.0;
			for (const float v : momentaryWindow)
				sp += std::pow(10.0, static_cast<double>(v) / 10.0);
			return static_cast<float>(10.0 * std::log10(sp / momentaryWindow.size() + 1e-30));
		}();

		// Short-term LUFS (power-average over 3 s window)
		float shortTermLufs = [&]() -> float {
			if (shortTermWindow.empty()) return kSilenceLufs;
			double sp = 0.0;
			for (const float v : shortTermWindow)
				sp += std::pow(10.0, static_cast<double>(v) / 10.0);
			return static_cast<float>(10.0 * std::log10(sp / shortTermWindow.size() + 1e-30));
		}();

		// Accumulate the 400 ms MOMENTARY block for integrated loudness
		// (EBU R128 gates momentary blocks, not short-term values), stored
		// as power. Pass 1 (absolute gate) is maintained incrementally.
		const float blockPower = std::pow(10.0f, momentaryLufs / 10.0f);
		integratedPowers.push_back(blockPower);
		if (blockPower >= kAbsGatePower)
		{
			absGatedPowerSum += blockPower;
			++absGatedCount;
		}

		float integratedLufs = kSilenceLufs;
		if (absGatedCount > 0)
		{
			// Pass 2: relative gate at -10 LU below the ungated mean
			const double relGatePower =
				(absGatedPowerSum / static_cast<double>(absGatedCount))
				* kRelGatePowerRatio;

			double sumPower2 = 0.0;
			int    count2    = 0;
			for (const float p : integratedPowers)
			{
				if (p >= kAbsGatePower && p >= relGatePower)
				{
					sumPower2 += p;
					++count2;
				}
			}

			if (count2 > 0)
				integratedLufs = static_cast<float>(
					10.0 * std::log10(sumPower2 / static_cast<double>(count2) + 1e-30));
		}

		// Dynamic range from short-term window
		float dynamicRange = 0.0f;
		if (!shortTermWindow.empty())
		{
			auto [minIt, maxIt] = std::minmax_element(
				shortTermWindow.cbegin(), shortTermWindow.cend());
			dynamicRange = *maxIt - *minIt;
		}

		// Store results
		result.cache[0][static_cast<size_t>(f)] = loudnessLufs;
		result.cache[1][static_cast<size_t>(f)] = momentaryLufs;
		result.cache[2][static_cast<size_t>(f)] = shortTermLufs;
		result.cache[3][static_cast<size_t>(f)] = integratedLufs;
		result.cache[4][static_cast<size_t>(f)] = dynamicRange;
		result.cache[5][static_cast<size_t>(f)] = rms;
		result.cache[6][static_cast<size_t>(f)] = zcr;
	}

	delete zcrAlgo;

	result.numFrames  = numFrames;
	result.sampleRate = static_cast<float>(sampleRate / static_cast<double>(frameSize));
	result.success    = true;
	progress.store(1.0f, std::memory_order_relaxed);
	return result;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

UNIFIED_CHOP_DLL_EXPORT(EssentiaLoudnessCHOP, "Essentialoudness", "Essentia Loudness", "ESL")
