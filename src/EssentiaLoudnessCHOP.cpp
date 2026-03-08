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
static constexpr float  kRelativeGateOffset = -10.0f; // EBU relative gate: -10 LU
static constexpr float  kSilenceDb         = -144.0f; // floor for empty windows

// ---------------------------------------------------------------------------
// CRTP hook: getOutputInfoImpl
// ---------------------------------------------------------------------------

bool EssentiaLoudnessCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                              const OP_Inputs* inputs,
                                              bool isBatch)
{
	// Normalize sub-parameters — enable when Normalize is on
	const bool normalize = ParametersLoudness::evalNormalize(inputs);
	inputs->enablePar(DbfloorName,   normalize);
	inputs->enablePar(DbceilingName, normalize);

	// Loudness does not use FFT params — nothing else to show/hide

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
		double rate = inputs->getTimeInfo()->rate;
		if (rate <= 0.0) rate = 60.0;
		info->sampleRate = static_cast<float>(rate);
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
	const float gateThreshDb = ParametersLoudness::evalGatethreshold(inputs);
	const bool  normalize    = ParametersLoudness::evalNormalize(inputs);
	const float dbFloor      = ParametersLoudness::evalDbfloor(inputs);
	const float dbCeiling    = ParametersLoudness::evalDbceiling(inputs);
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
			configureAlgorithms(frameSize, zcrThreshold);

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
			myIntegratedValues.clear();

			// Reset output values
			myLoudnessDb       = kSilenceDb;
			myMomentaryLoudness  = kSilenceDb;
			myShortTermLoudness  = kSilenceDb;
			myIntegratedLoudness = kSilenceDb;
			myDynamicRange       = 0.0f;
			myLoudnessDbMin      = 0.0f;
			myLoudnessDbMax      = kSilenceDb;
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
			recomputeIntegrated(gateThreshDb);
			myHopCounter = 0;
		}
	}

	// Prepare output values, applying normalization if enabled
	float outLoudness   = myLoudnessDb;
	float outMomentary  = myMomentaryLoudness;
	float outShortTerm  = myShortTermLoudness;
	float outIntegrated = myIntegratedLoudness;
	float outDynRange   = myDynamicRange;

	if (normalize)
	{
		const float range = dbCeiling - dbFloor;
		auto norm = [&](float dB) -> float
		{
			return (range > 0.0f)
			       ? std::clamp((dB - dbFloor) / range, 0.0f, 1.0f)
			       : 0.0f;
		};
		outLoudness   = norm(outLoudness);
		outMomentary  = norm(outMomentary);
		outShortTerm  = norm(outShortTerm);
		outIntegrated = norm(outIntegrated);
		// dynamic_range is a delta, not an absolute dB level
		outDynRange   = (range > 0.0f)
		                ? std::clamp(outDynRange / range, 0.0f, 1.0f)
		                : 0.0f;
	}

	// Write output — repeat latest values for the whole time slice
	for (int s = 0; s < output->numSamples; ++s)
	{
		output->channels[0][s] = outLoudness;
		output->channels[1][s] = outMomentary;
		output->channels[2][s] = outShortTerm;
		output->channels[3][s] = outIntegrated;
		output->channels[4][s] = outDynRange;
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
	params.gateThreshDb = ParametersLoudness::evalGatethreshold(inputs);
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
                                             const OP_Inputs* inputs)
{
	if (myHasResults)
	{
		const bool  normalize = ParametersLoudness::evalNormalize(inputs);
		const float dbFloor   = ParametersLoudness::evalDbfloor(inputs);
		const float dbCeiling = ParametersLoudness::evalDbceiling(inputs);
		const float range     = dbCeiling - dbFloor;

		const int numCh   = std::min(output->numChannels,
		                             static_cast<int>(myResultCache.size()));
		const int numSamp = output->numSamples;

		for (int c = 0; c < numCh; ++c)
		{
			const auto& chData = myResultCache[c];
			const bool isLoudnessCh = (c <= 4); // channels 0-4 are dB-scale values

			for (int s = 0; s < numSamp; ++s)
			{
				float val = (s < static_cast<int>(chData.size()))
				            ? chData[s] : 0.0f;

				if (normalize && isLoudnessCh && range > 0.0f)
				{
					if (c == 4) // dynamic_range is a delta, not absolute dB
						val = std::clamp(val / range, 0.0f, 1.0f);
					else
						val = std::clamp((val - dbFloor) / range, 0.0f, 1.0f);
				}
				output->channels[c][s] = val;
			}
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
		chan->name->setString("loudness_db_min");
		chan->value = myLoudnessDbMin;
		break;
	case 6:
		chan->name->setString("loudness_db_max");
		chan->value = myLoudnessDbMax;
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Algorithm management
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::configureAlgorithms(int /*frameSize*/,
                                                float zcrThreshold)
{
	releaseAlgorithms();

	myLoudnessAlgo = AlgorithmFactory::create("Loudness");
	myZcrAlgo      = AlgorithmFactory::create("ZeroCrossingRate",
		"threshold", static_cast<Real>(zcrThreshold));
}

void EssentiaLoudnessCHOP::releaseAlgorithms()
{
	delete myLoudnessAlgo;
	myLoudnessAlgo = nullptr;
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

	// Compute RMS from raw audio frame
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

	// Run Essentia Loudness algorithm
	if (myLoudnessAlgo)
	{
		try {
			myLoudnessAlgo->input("signal").set(myAudioFrame);
			myLoudnessAlgo->output("loudness").set(myEssentiaLoudness);
			myLoudnessAlgo->compute();
		} catch (...) { myEssentiaLoudness = 0.0f; }
	}

	// Convert Essentia's linear-power loudness to a dB value.
	// Essentia's Loudness returns mean(x^2) — mean-square power.
	// Map to dBFS-like: 10 * log10(power + 1e-10).
	const float power = static_cast<float>(myEssentiaLoudness);
	myLoudnessDb = 10.0f * std::log10(power + 1e-10f);

	// Track running min/max for normalization guidance
	if (myLoudnessDb > myLoudnessDbMax) myLoudnessDbMax = myLoudnessDb;
	if (myLoudnessDb < myLoudnessDbMin) myLoudnessDbMin = myLoudnessDb;

	// Push into momentary window
	myMomentaryWindow.push_back(myLoudnessDb);
	if (static_cast<int>(myMomentaryWindow.size()) > myMomentaryCapacity)
		myMomentaryWindow.pop_front();

	// Push into short-term window
	myShortTermWindow.push_back(myLoudnessDb);
	if (static_cast<int>(myShortTermWindow.size()) > myShortTermCapacity)
		myShortTermWindow.pop_front();

	// Update windowed loudness outputs
	myMomentaryLoudness = windowedLoudnessDb(myMomentaryWindow);
	myShortTermLoudness = windowedLoudnessDb(myShortTermWindow);

	// Accumulate short-term value for integrated loudness computation.
	// Cap at ~1 hour to prevent unbounded growth.
	static constexpr size_t kMaxIntegratedEntries = 200000;
	if (myIntegratedValues.size() >= kMaxIntegratedEntries)
		myIntegratedValues.erase(myIntegratedValues.begin(),
		                         myIntegratedValues.begin()
		                         + static_cast<ptrdiff_t>(myIntegratedValues.size() / 4));
	myIntegratedValues.push_back(myShortTermLoudness);

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
// Static helper: power-average of a deque of dB values
// ---------------------------------------------------------------------------

float EssentiaLoudnessCHOP::windowedLoudnessDb(const std::deque<float>& window)
{
	if (window.empty())
		return kSilenceDb;

	double sumPower = 0.0;
	for (const float db : window)
		sumPower += std::pow(10.0, static_cast<double>(db) / 10.0);

	const double meanPower = sumPower / static_cast<double>(window.size());
	return static_cast<float>(10.0 * std::log10(meanPower + 1e-30));
}

// ---------------------------------------------------------------------------
// EBU R128 integrated loudness (two-pass gating)
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::recomputeIntegrated(float gateThreshDb)
{
	if (myIntegratedValues.empty())
	{
		myIntegratedLoudness = kSilenceDb;
		return;
	}

	// Pass 1: absolute gate — collect values above absolute threshold
	std::vector<float> pass1;
	pass1.reserve(myIntegratedValues.size());
	for (const float v : myIntegratedValues)
	{
		if (v >= gateThreshDb)
			pass1.push_back(v);
	}

	if (pass1.empty())
	{
		myIntegratedLoudness = kSilenceDb;
		return;
	}

	// Compute ungated mean of pass-1 values in power domain
	double sumPower = 0.0;
	for (const float v : pass1)
		sumPower += std::pow(10.0, static_cast<double>(v) / 10.0);
	const double ungatedMeanPower = sumPower / static_cast<double>(pass1.size());
	const float  ungatedMeanDb    =
		static_cast<float>(10.0 * std::log10(ungatedMeanPower + 1e-30));

	// Pass 2: relative gate — keep values >= ungatedMean - 10 LU
	const float relativeGateDb = ungatedMeanDb + kRelativeGateOffset;

	double sumPower2 = 0.0;
	int    count2    = 0;
	for (const float v : pass1)
	{
		if (v >= relativeGateDb)
		{
			sumPower2 += std::pow(10.0, static_cast<double>(v) / 10.0);
			++count2;
		}
	}

	if (count2 == 0)
	{
		myIntegratedLoudness = kSilenceDb;
		return;
	}

	const double integratedMeanPower = sumPower2 / static_cast<double>(count2);
	myIntegratedLoudness =
		static_cast<float>(10.0 * std::log10(integratedMeanPower + 1e-30));
}

// ===========================================================================
// Static async worker — no `this`, creates local Essentia algorithms
// ===========================================================================

AsyncBatchResult EssentiaLoudnessCHOP::computeBatchAsync(
	const AudioSnapshot&       audio,
	const BatchLoudnessParams& params,
	const std::atomic<bool>&   cancelFlag,
	std::atomic<float>&        progress)
{
	AsyncBatchResult result;
	result.success = false;

	const int   frameSize    = params.frameSize;
	const float gateThreshDb = params.gateThreshDb;

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

	// Create local algorithms
	Algorithm* loudnessAlgo = AlgorithmFactory::create("Loudness");
	Algorithm* zcrAlgo      = AlgorithmFactory::create("ZeroCrossingRate",
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
	std::vector<float> integratedValues;
	integratedValues.reserve(static_cast<size_t>(numFrames));

	std::vector<Real> frame(static_cast<size_t>(frameSize), 0.0f);
	Real essentiaLoudness = 0.0f;
	Real essentiaZcr      = 0.0f;

	const float* audioData = audio.data.data();

	for (int f = 0; f < numFrames; ++f)
	{
		if (cancelFlag.load(std::memory_order_relaxed))
		{
			delete loudnessAlgo;
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

		// Loudness
		try {
			loudnessAlgo->input("signal").set(frame);
			loudnessAlgo->output("loudness").set(essentiaLoudness);
			loudnessAlgo->compute();
		} catch (...) { essentiaLoudness = 0.0f; }

		float loudnessDb =
			10.0f * std::log10(static_cast<float>(essentiaLoudness) + 1e-10f);

		// RMS
		double sum = 0.0;
		for (int i = 0; i < frameSize; ++i)
			sum += static_cast<double>(frame[static_cast<size_t>(i)])
			     * static_cast<double>(frame[static_cast<size_t>(i)]);
		float rms = static_cast<float>(std::sqrt(sum / static_cast<double>(frameSize)));

		// ZCR
		float zcr = 0.0f;
		try {
			zcrAlgo->input("signal").set(frame);
			zcrAlgo->output("zeroCrossingRate").set(essentiaZcr);
			zcrAlgo->compute();
			zcr = static_cast<float>(essentiaZcr);
		} catch (...) {}

		// Momentary window
		momentaryWindow.push_back(loudnessDb);
		if (static_cast<int>(momentaryWindow.size()) > momentaryCap)
			momentaryWindow.pop_front();

		// Short-term window
		shortTermWindow.push_back(loudnessDb);
		if (static_cast<int>(shortTermWindow.size()) > shortTermCap)
			shortTermWindow.pop_front();

		float momentaryLoudness = [&]() -> float {
			if (momentaryWindow.empty()) return kSilenceDb;
			double sp = 0.0;
			for (const float db : momentaryWindow)
				sp += std::pow(10.0, static_cast<double>(db) / 10.0);
			return static_cast<float>(10.0 * std::log10(sp / momentaryWindow.size() + 1e-30));
		}();

		float shortTermLoudness = [&]() -> float {
			if (shortTermWindow.empty()) return kSilenceDb;
			double sp = 0.0;
			for (const float db : shortTermWindow)
				sp += std::pow(10.0, static_cast<double>(db) / 10.0);
			return static_cast<float>(10.0 * std::log10(sp / shortTermWindow.size() + 1e-30));
		}();

		// Accumulate short-term for integrated loudness (two-pass gating)
		integratedValues.push_back(shortTermLoudness);

		// Pass 1: absolute gate
		double sumPower1 = 0.0;
		int    count1    = 0;
		for (const float v : integratedValues)
		{
			if (v >= gateThreshDb)
			{
				sumPower1 += std::pow(10.0, static_cast<double>(v) / 10.0);
				++count1;
			}
		}

		float integratedLoudness = kSilenceDb;
		if (count1 > 0)
		{
			float ungatedMeanDb = static_cast<float>(
				10.0 * std::log10(sumPower1 / static_cast<double>(count1) + 1e-30));
			float relGate = ungatedMeanDb + kRelativeGateOffset;

			// Pass 2: relative gate
			double sumPower2 = 0.0;
			int    count2    = 0;
			for (const float v : integratedValues)
			{
				if (v >= gateThreshDb && v >= relGate)
				{
					sumPower2 += std::pow(10.0, static_cast<double>(v) / 10.0);
					++count2;
				}
			}

			if (count2 > 0)
				integratedLoudness = static_cast<float>(
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
		result.cache[0][static_cast<size_t>(f)] = loudnessDb;
		result.cache[1][static_cast<size_t>(f)] = momentaryLoudness;
		result.cache[2][static_cast<size_t>(f)] = shortTermLoudness;
		result.cache[3][static_cast<size_t>(f)] = integratedLoudness;
		result.cache[4][static_cast<size_t>(f)] = dynamicRange;
		result.cache[5][static_cast<size_t>(f)] = rms;
		result.cache[6][static_cast<size_t>(f)] = zcr;
	}

	delete loudnessAlgo;
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
