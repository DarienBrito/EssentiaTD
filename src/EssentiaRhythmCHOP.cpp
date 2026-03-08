// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaRhythmCHOP.h"
#include "Shared/Utils.h"
#include "Shared/BatchFrameProcessor.h"

#include <essentia/essentiamath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// EMA coefficient for BPM smoothing (lower = more inertia)
static constexpr float kBpmSmoothingAlpha = 0.10f;
/// Minimum autocorrelation peak to avoid divide-by-zero
static constexpr float kMinAutocorrPeak   = 1e-6f;

/// Channel name table — index must match output channel order in writeOutputs()
static const char* const kChannelNames[EssentiaRhythmCHOP::kNumOutputChannels] = {
	"onset",
	"onset_strength",
	"bpm",
	"beat",
	"beat_phase",
	"beat_confidence",
};

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

EssentiaRhythmCHOP::EssentiaRhythmCHOP(const OP_NodeInfo* info)
	: UnifiedCHOPBase<EssentiaRhythmCHOP>(info)
{
	myOnsetHistory.resize(kOnsetHistorySize, 0.0f);
}

EssentiaRhythmCHOP::~EssentiaRhythmCHOP()
{
	releaseAlgorithms();
}

// ---------------------------------------------------------------------------
// getOutputInfoImpl
// ---------------------------------------------------------------------------

bool EssentiaRhythmCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                             const OP_Inputs* inputs,
                                             bool isBatch)
{
	// RT-only params
	const bool tempoBias = ParametersRhythm::evalTempobias(inputs);
	inputs->enablePar(TempobiasName,  !isBatch);
	inputs->enablePar(BiascenterName, !isBatch && tempoBias);

	// Batch-only params: Rhythm Method + FFT params
	inputs->enablePar(RhythmmethodName,      isBatch);
	inputs->enablePar(BatchFftsizeName,      isBatch);
	inputs->enablePar(BatchHopsizeName,      isBatch);
	inputs->enablePar(BatchWindowtypeName,   isBatch);
	inputs->enablePar(BatchZeropaddingName,  isBatch);

	info->numChannels = kNumOutputChannels;

	if (isBatch)
	{
		info->numSamples = myHasResults ? myCachedNumFrames : 1;
		info->startIndex = 0;
		info->sampleRate = myHasResults ? myCachedSampleRate : 1.0f;
	}
	else
	{
		info->numSamples = 1;
		info->sampleRate = static_cast<float>(inputs->getTimeInfo()->rate);
	}

	return true;
}

// ---------------------------------------------------------------------------
// getChannelNameImpl
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::getChannelNameImpl(int32_t index,
                                              OP_String* name,
                                              const OP_Inputs* /*inputs*/)
{
	if (index >= 0 && index < kNumOutputChannels)
		name->setString(kChannelNames[index]);
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------
// executeRealtimeImpl — full real-time processing pipeline
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::executeRealtimeImpl(CHOP_Output* output,
                                               const OP_Inputs* inputs)
{
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		zeroOutput(output);
		return;
	}

	// Reset per-frame trigger outputs before any early-return
	myOutOnset = 0.0f;
	myOutBeat  = 0.0f;

	// ---- Read parameters ----
	const int   onsetMethodIdx = ParametersRhythm::evalOnsetmethod(inputs);
	const float sensitivity    = ParametersRhythm::evalOnsetsensitivity(inputs);
	const int   bpmMin         = ParametersRhythm::evalBpmmin(inputs);
	int         bpmMax         = ParametersRhythm::evalBpmmax(inputs);
	if (bpmMax <= bpmMin) bpmMax = bpmMin + 1;
	const bool  tempoBias  = ParametersRhythm::evalTempobias(inputs);
	const float biasCenter = ParametersRhythm::evalBiascenter(inputs);

	static const char* kMethodNames[] = { "hfc", "complex", "flux", "melflux", "rms" };
	const int   clampedMethodIdx = std::clamp(onsetMethodIdx, 0, 4);
	const char* onsetMethod      = kMethodNames[clampedMethodIdx];

	// ---- Read input CHOP ----
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1 || chopIn->numSamples < 1)
	{
		myError = "No valid input CHOP connected";
		writeOutputs(output);
		return;
	}

	double sampleRate = chopIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	// ---- Extract spectrum magnitude from named channel ----
	std::vector<float> specMagFloat;
	const bool hasSpectrum = extractChannelSamples(chopIn, "spectrum", specMagFloat);

	if (!hasSpectrum || specMagFloat.empty())
	{
		myWarning = "No spectrum channel found in input — connect EssentiaSpectrumCHOP";
		updateBeatPhase(bpmMin, bpmMax);
		writeOutputs(output);
		return;
	}

	const int specSize = static_cast<int>(specMagFloat.size());

	// ---- Reconfigure if spectrum size, method, or sample rate changed ----
	if (specSize != mySpecSize
	    || std::strcmp(onsetMethod, myCurrentMethod.c_str()) != 0
	    || sampleRate != mySampleRate)
	{
		try
		{
			configureOnsetDetection(specSize, onsetMethod, sampleRate);
			mySpecSize      = specSize;
			mySampleRate    = sampleRate;
			myCurrentMethod = onsetMethod;
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

	// ---- Estimate hop size from sample rate and fps ----
	// hopSize = sampleRate / fps  (approximate — hop is not directly exposed here).
	++myFrameCount;
	const int approxHopSize = static_cast<int>(sampleRate / myFpsEstimate);
	myHopSize = (approxHopSize > 0) ? approxHopSize : 512;

	// ---- Run OnsetDetection ----
	mySpectrumBuf.assign(specMagFloat.begin(), specMagFloat.end());
	myPhaseBuf.assign(specSize, 0.0f); // zero-phase approximation (magnitude-only input)

	myOnsetValue = 0.0f;
	if (myOnsetDetection)
	{
		try
		{
			myOnsetDetection->input("spectrum").set(mySpectrumBuf);
			myOnsetDetection->input("phase").set(myPhaseBuf);
			myOnsetDetection->output("onsetDetection").set(myOnsetValue);
			myOnsetDetection->compute();
		}
		catch (const std::exception& e)
		{
			myError = std::string("OnsetDetection error: ") + e.what();
			writeOutputs(output);
			return;
		}
	}

	myOutOnsetStrength = static_cast<float>(myOnsetValue);

	// ---- Onset trigger (adaptive normalised threshold) ----
	// sensitivity=0 → very high threshold (rarely fires)
	// sensitivity=1 → very low threshold (fires often)
	myRunningMax = std::max(myRunningMax * 0.999f, myOutOnsetStrength);

	const float normalised = (myRunningMax > kMinAutocorrPeak)
	                         ? (myOutOnsetStrength / myRunningMax)
	                         : 0.0f;

	const float threshold = 1.0f - sensitivity;
	myOutOnset = (normalised >= threshold) ? 1.0f : 0.0f;

	// ---- Push onset strength into circular history buffer ----
	pushOnsetStrength(myOutOnsetStrength);

	// ---- Compute autocorrelation BPM ----
	const float rawBpm = computeAutocorrBpm(bpmMin, bpmMax, sampleRate, myHopSize,
	                                         tempoBias, biasCenter);
	if (rawBpm > 0.0f)
	{
		myBpmMedianBuf[myBpmMedianPos] = rawBpm;
		myBpmMedianPos = (myBpmMedianPos + 1) % kBpmMedianSize;
		if (myBpmMedianFill < kBpmMedianSize)
			++myBpmMedianFill;

		std::array<float, kBpmMedianSize> sorted;
		for (int i = 0; i < myBpmMedianFill; ++i)
			sorted[i] = myBpmMedianBuf[i];
		std::sort(sorted.begin(), sorted.begin() + myBpmMedianFill);
		const float medianBpm = sorted[myBpmMedianFill / 2];

		mySmoothedBpm = mySmoothedBpm * (1.0f - kBpmSmoothingAlpha)
		                + medianBpm * kBpmSmoothingAlpha;
		mySmoothedBpm = std::clamp(mySmoothedBpm, static_cast<float>(bpmMin),
		                                           static_cast<float>(bpmMax));
	}
	myOutBpm = mySmoothedBpm;

	// ---- Beat phase & beat trigger ----
	updateBeatPhase(bpmMin, bpmMax);

	// ---- Commit outputs ----
	writeOutputs(output);
}

// ---------------------------------------------------------------------------
// snapshotAndLaunch — capture params and start async batch worker
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::snapshotAndLaunch(AudioSnapshot audio,
                                             const OP_Inputs* inputs)
{
	BatchRhythmParams params;
	params.fftSize       = evalBatchFftsize(inputs);
	params.hopSize       = evalBatchHopsize(inputs);
	params.windowType    = evalBatchWindowtype(inputs);
	params.zeroPad       = evalBatchZeropadding(inputs);
	params.rhythmMethod  = ParametersRhythm::evalRhythmmethod(inputs);
	params.onsetMethodIdx = ParametersRhythm::evalOnsetmethod(inputs);
	params.sensitivity   = ParametersRhythm::evalOnsetsensitivity(inputs);
	params.bpmMin        = ParametersRhythm::evalBpmmin(inputs);
	params.bpmMax        = ParametersRhythm::evalBpmmax(inputs);
	if (params.bpmMax <= params.bpmMin)
		params.bpmMax = params.bpmMin + 1;

	myRunner.launch([snap = std::move(audio), params]
		(const std::atomic<bool>& cancelFlag, std::atomic<float>& progress)
	{
		return computeBatchAsync(snap, params, cancelFlag, progress);
	});
}

// ---------------------------------------------------------------------------
// onResultCollected — extract batch-specific fields
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::onResultCollected(AsyncBatchResult& result)
{
	myDetectedBpm  = result.detectedBpm;
	myUsedAutocorr = result.usedAutocorr;
}

// ---------------------------------------------------------------------------
// setupParametersImpl
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::setupParametersImpl(OP_ParameterManager* manager)
{
	ParametersRhythm::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP — 6 channels
// ---------------------------------------------------------------------------

int32_t EssentiaRhythmCHOP::getNumInfoCHOPChansImpl()
{
	return 6;
}

void EssentiaRhythmCHOP::getInfoCHOPChanImpl(int32_t index,
                                               OP_InfoCHOPChan* chan)
{
	switch (index)
	{
	case 0:
		chan->name->setString("onset_buffer_fill");
		chan->value = static_cast<float>(myOnsetFillCount);
		break;
	case 1:
		chan->name->setString("current_bpm");
		chan->value = mySmoothedBpm;
		break;
	case 2:
		chan->name->setString("detected_bpm");
		chan->value = myDetectedBpm;
		break;
	case 3:
		chan->name->setString("used_autocorr");
		chan->value = myUsedAutocorr ? 1.0f : 0.0f;
		break;
	case 4:
		chan->name->setString("computing");
		chan->value = myRunner.isComputing() ? 1.0f : 0.0f;
		break;
	case 5:
		chan->name->setString("progress");
		chan->value = myRunner.progress();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Private RT helpers
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::configureOnsetDetection(int specSize,
                                                   const char* method,
                                                   double sampleRate)
{
	releaseAlgorithms();

	mySpectrumBuf.assign(specSize, 0.0f);
	myPhaseBuf.assign(specSize, 0.0f);

	// Reset median BPM buffer on reconfigure to avoid stale data
	myBpmMedianBuf.fill(0.0f);
	myBpmMedianPos  = 0;
	myBpmMedianFill = 0;

	myOnsetDetection = AlgorithmFactory::create("OnsetDetection",
		"method",     std::string(method),
		"sampleRate", static_cast<Real>(sampleRate));
}

void EssentiaRhythmCHOP::releaseAlgorithms()
{
	delete myOnsetDetection;
	myOnsetDetection = nullptr;
}

void EssentiaRhythmCHOP::pushOnsetStrength(float value)
{
	myOnsetHistory[myOnsetWritePos] = value;
	myOnsetWritePos = (myOnsetWritePos + 1) % kOnsetHistorySize;
	if (myOnsetFillCount < kOnsetHistorySize)
		++myOnsetFillCount;
}

float EssentiaRhythmCHOP::computeAutocorrBpm(int bpmMin, int bpmMax,
                                               double sampleRate,
                                               int hopSize,
                                               bool tempoBias,
                                               float biasCenter) const
{
	const int framesPerSecond = (hopSize > 0)
	                            ? static_cast<int>(sampleRate / static_cast<double>(hopSize))
	                            : 60;

	const int lagMin = static_cast<int>(60.0 * framesPerSecond / static_cast<double>(bpmMax));
	const int lagMax = static_cast<int>(60.0 * framesPerSecond / static_cast<double>(bpmMin));

	if (lagMin < 1 || lagMax < lagMin)
		return 0.0f;

	// Need enough history for harmonic summation: 4 * lagMax
	if (myOnsetFillCount < lagMax * 4 + 1)
	{
		if (myOnsetFillCount < lagMax + 1)
			return 0.0f;
	}

	// Build a contiguous view of the circular buffer (oldest first)
	const int histLen = std::min(myOnsetFillCount, kOnsetHistorySize);
	std::vector<float> buf(histLen);
	{
		int readStart = (myOnsetFillCount < kOnsetHistorySize) ? 0 : myOnsetWritePos;
		for (int i = 0; i < histLen; ++i)
			buf[i] = myOnsetHistory[(readStart + i) % kOnsetHistorySize];
	}

	// Zero-centred mean
	const float mean = std::accumulate(buf.begin(), buf.end(), 0.0f)
	                   / static_cast<float>(histLen);

	// Zero-lag energy R(0)
	double r0 = 0.0;
	for (int i = 0; i < histLen; ++i)
	{
		const double v = static_cast<double>(buf[i] - mean);
		r0 += v * v;
	}
	if (r0 < static_cast<double>(kMinAutocorrPeak))
		return 0.0f;

	// Pre-compute autocorrelation for all lags up to 4 * lagMax
	const int maxNeededLag = std::min(lagMax * 4, histLen - 1);
	std::vector<double> autocorr(maxNeededLag + 1, 0.0);
	for (int lag = lagMin; lag <= maxNeededLag; ++lag)
	{
		double r = 0.0;
		int    n = 0;
		for (int i = 0; i + lag < histLen; ++i)
		{
			r += static_cast<double>(buf[i] - mean)
			   * static_cast<double>(buf[i + lag] - mean);
			++n;
		}
		if (n > 0)
			autocorr[lag] = r / static_cast<double>(n);
	}

	// Harmonic summation: for each candidate lag sum autocorrelation at
	// harmonics h=1..4, weighted by 1/h; optionally apply Gaussian tempo prior.
	int    bestLag   = lagMin;
	double bestScore = -1e9;

	static constexpr int    kNumHarmonics = 4;
	static constexpr double kBiasSigma    = 40.0; // BPM std-dev for Gaussian prior

	for (int lag = lagMin; lag <= lagMax; ++lag)
	{
		double score = 0.0;
		for (int h = 1; h <= kNumHarmonics; ++h)
		{
			const int hLag = h * lag;
			if (hLag <= maxNeededLag)
				score += autocorr[hLag] / static_cast<double>(h);
		}

		if (tempoBias && lag > 0)
		{
			const double bpm  = 60.0 * framesPerSecond / static_cast<double>(lag);
			const double diff = (bpm - static_cast<double>(biasCenter)) / kBiasSigma;
			score *= std::exp(-0.5 * diff * diff);
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestLag   = lag;
		}
	}

	// Update confidence: normalised harmonic score
	const float confidence = static_cast<float>(
		std::clamp(bestScore / (r0 / static_cast<double>(histLen)), 0.0, 1.0));
	myBeatConfidence = confidence;

	if (bestLag <= 0)
		return 0.0f;

	return 60.0f * static_cast<float>(framesPerSecond) / static_cast<float>(bestLag);
}

void EssentiaRhythmCHOP::updateBeatPhase(int bpmMin, int bpmMax)
{
	const float clampedBpm = std::clamp(mySmoothedBpm,
	                                    static_cast<float>(bpmMin),
	                                    static_cast<float>(bpmMax));

	// Phase increment per frame: bpm / (fps * 60)
	const float phaseIncrement = clampedBpm / static_cast<float>(myFpsEstimate * 60.0);
	myBeatPhase += phaseIncrement;

	if (myBeatPhase >= 1.0f)
	{
		myBeatPhase = std::fmod(myBeatPhase, 1.0f);
		myOutBeat   = 1.0f;
	}

	myOutBpm            = clampedBpm;
	myOutBeatPhase      = myBeatPhase;
	myOutBeatConfidence = myBeatConfidence;
}

void EssentiaRhythmCHOP::writeOutputs(CHOP_Output* output)
{
	if (!output || output->numChannels < kNumOutputChannels)
		return;

	// Channel order: onset, onset_strength, bpm, beat, beat_phase, beat_confidence
	output->channels[0][0] = myOutOnset;
	output->channels[1][0] = myOutOnsetStrength;
	output->channels[2][0] = myOutBpm;
	output->channels[3][0] = myOutBeat;
	output->channels[4][0] = myOutBeatPhase;
	output->channels[5][0] = myOutBeatConfidence;
}

// ---------------------------------------------------------------------------
// Static async batch worker
// ---------------------------------------------------------------------------

AsyncBatchResult EssentiaRhythmCHOP::computeBatchAsync(
	const AudioSnapshot&     audio,
	const BatchRhythmParams& params,
	const std::atomic<bool>& cancelFlag,
	std::atomic<float>&      progress)
{
	AsyncBatchResult result;

	const double sampleRate = (audio.sampleRate > 0.0) ? audio.sampleRate : 44100.0;
	const int audioLength   = audio.numSamples;
	const float* audioData  = audio.data.data();

	const int zeroPad = zeroPadFromFactor(params.zeroPad, params.fftSize);

	static const char* kOnsetMethods[] = { "hfc", "complex", "flux", "melflux", "rms" };
	const char* onsetMethod = kOnsetMethods[std::clamp(params.onsetMethodIdx, 0, 4)];

	// ---- Per-frame onset detection via FFT ----
	BatchFrameProcessor frameProc;
	frameProc.configure(params.fftSize, params.hopSize, params.windowType, zeroPad);
	frameProc.processAllFrames(audioData, audioLength);

	const int numFrames = frameProc.numFrames();
	if (numFrames == 0)
	{
		result.warning = "Audio too short for given FFT size";
		result.success  = false;
		return result;
	}

	Algorithm* onsetDetection = AlgorithmFactory::create("OnsetDetection",
		"method",     std::string(onsetMethod),
		"sampleRate", static_cast<Real>(sampleRate));

	std::vector<float> onsetStrength(numFrames, 0.0f);
	std::vector<Real>  phaseBuf(frameProc.specBins(), 0.0f);
	Real onsetValue = 0.0f;

	for (int f = 0; f < numFrames; ++f)
	{
		if (cancelFlag.load(std::memory_order_relaxed))
		{
			delete onsetDetection;
			result.success = false;
			return result;
		}

		const auto& spectrum = frameProc.getSpectrum(f);
		try {
			onsetDetection->input("spectrum").set(spectrum);
			onsetDetection->input("phase").set(phaseBuf);
			onsetDetection->output("onsetDetection").set(onsetValue);
			onsetDetection->compute();
			onsetStrength[f] = static_cast<float>(onsetValue);
		} catch (...) {}

		// Progress: 0–50% across onset frames
		progress.store(0.5f * static_cast<float>(f + 1) / static_cast<float>(numFrames),
		               std::memory_order_relaxed);
	}

	delete onsetDetection;
	frameProc.release();

	// ---- Onset binary triggers (adaptive threshold) ----
	std::vector<float> onsetBinary(numFrames, 0.0f);
	{
		float runningMax      = 1e-4f;
		const float threshold = 1.0f - params.sensitivity;
		for (int f = 0; f < numFrames; ++f)
		{
			runningMax = std::max(runningMax * 0.999f, onsetStrength[f]);
			const float normalised = (runningMax > 1e-6f)
				? (onsetStrength[f] / runningMax) : 0.0f;
			onsetBinary[f] = (normalised >= threshold) ? 1.0f : 0.0f;
		}
	}

	// ---- BPM + beat detection ----
	float detectedBpm    = 0.0f;
	float beatConfidence = 0.0f;
	std::vector<int> beatFrames;
	bool rhythmExtractorOk = false;

	if (cancelFlag.load(std::memory_order_relaxed))
	{
		result.success = false;
		return result;
	}

	// Try RhythmExtractor2013 first
	{
		static const char* methodNames[] = { "multifeature", "degara" };
		const char* method = methodNames[std::clamp(params.rhythmMethod, 0, 1)];

		Algorithm* rhythmExtractor = nullptr;
		try {
			rhythmExtractor = AlgorithmFactory::create("RhythmExtractor2013",
				"method",   std::string(method),
				"minTempo", static_cast<int>(params.bpmMin),
				"maxTempo", static_cast<int>(params.bpmMax));
		}
		catch (const std::exception& e) {
			result.warning = std::string("RhythmExtractor2013 failed: ") + e.what()
				+ " — using autocorrelation fallback";
		}
		catch (...) {
			result.warning = "RhythmExtractor2013 failed — using autocorrelation fallback";
		}

		if (rhythmExtractor)
		{
			try {
				std::vector<Real> audioSignal(audioLength);
				for (int i = 0; i < audioLength; ++i)
					audioSignal[i] = static_cast<Real>(audioData[i]);

				Real bpmOut  = 0.0f;
				Real confOut = 0.0f;
				std::vector<Real> ticks;
				std::vector<Real> estimates;
				std::vector<Real> bpmIntervals;

				rhythmExtractor->input("signal").set(audioSignal);
				rhythmExtractor->output("bpm").set(bpmOut);
				rhythmExtractor->output("ticks").set(ticks);
				rhythmExtractor->output("confidence").set(confOut);
				rhythmExtractor->output("estimates").set(estimates);
				rhythmExtractor->output("bpmIntervals").set(bpmIntervals);

				// Progress: 50–90% during rhythm extraction (single heavy call)
				progress.store(0.5f, std::memory_order_relaxed);
				rhythmExtractor->compute();
				progress.store(0.9f, std::memory_order_relaxed);

				detectedBpm    = static_cast<float>(bpmOut);
				beatConfidence = static_cast<float>(confOut);

				const double secondsPerFrame =
					static_cast<double>(params.hopSize) / sampleRate;
				for (const auto& tickSec : ticks)
				{
					const int frameIdx = static_cast<int>(std::round(
						static_cast<double>(tickSec) / secondsPerFrame));
					if (frameIdx >= 0 && frameIdx < numFrames)
						beatFrames.push_back(frameIdx);
				}

				rhythmExtractorOk = (detectedBpm > 0.0f);
			}
			catch (const std::exception& e) {
				result.warning = std::string("RhythmExtractor2013 compute failed: ")
					+ e.what();
			}
			catch (...) {
				result.warning = "RhythmExtractor2013 compute failed (unknown)";
			}

			delete rhythmExtractor;
		}
	}

	if (cancelFlag.load(std::memory_order_relaxed))
	{
		result.success = false;
		return result;
	}

	// ---- Fallback: autocorrelation BPM on onset strength curve ----
	if (!rhythmExtractorOk)
	{
		const double framesPerSecond =
			sampleRate / static_cast<double>(params.hopSize);
		const int lagMin = std::max(1,
			static_cast<int>(60.0 * framesPerSecond / params.bpmMax));
		const int lagMax = std::max(lagMin + 1,
			static_cast<int>(60.0 * framesPerSecond / params.bpmMin));

		if (numFrames > lagMax + 1)
		{
			const float mean = std::accumulate(onsetStrength.begin(),
				onsetStrength.end(), 0.0f) / static_cast<float>(numFrames);

			double r0 = 0.0;
			for (int i = 0; i < numFrames; ++i)
			{
				const double v = static_cast<double>(onsetStrength[i] - mean);
				r0 += v * v;
			}

			if (r0 > 1e-6)
			{
				static constexpr int kNumHarmonics = 4;
				const int maxNeededLag =
					std::min(lagMax * kNumHarmonics, numFrames - 1);

				std::vector<double> autocorr(maxNeededLag + 1, 0.0);
				for (int lag = lagMin; lag <= maxNeededLag; ++lag)
				{
					if (cancelFlag.load(std::memory_order_relaxed))
					{
						result.success = false;
						return result;
					}

					double r = 0.0;
					int    n = 0;
					for (int i = 0; i + lag < numFrames; ++i)
					{
						r += static_cast<double>(onsetStrength[i] - mean)
						   * static_cast<double>(onsetStrength[i + lag] - mean);
						++n;
					}
					if (n > 0) autocorr[lag] = r / static_cast<double>(n);

					// Progress: 50–90% across autocorrelation lags
					progress.store(0.5f + 0.4f * static_cast<float>(lag - lagMin)
						/ static_cast<float>(std::max(1, maxNeededLag - lagMin)),
						std::memory_order_relaxed);
				}

				int    bestLag   = lagMin;
				double bestScore = -1e9;
				for (int lag = lagMin; lag <= lagMax; ++lag)
				{
					double score = 0.0;
					for (int h = 1; h <= kNumHarmonics; ++h)
					{
						const int hLag = h * lag;
						if (hLag <= maxNeededLag)
							score += autocorr[hLag] / static_cast<double>(h);
					}
					if (score > bestScore)
					{
						bestScore = score;
						bestLag   = lag;
					}
				}

				if (bestLag > 0)
				{
					detectedBpm = static_cast<float>(
						60.0 * framesPerSecond / static_cast<double>(bestLag));
					beatConfidence = static_cast<float>(
						std::clamp(bestScore / (r0 / numFrames), 0.0, 1.0));
				}
			}
		}

		// Build beat positions from onset peaks spaced near detected BPM period
		if (detectedBpm > 0.0f)
		{
			const double framesPerSecond2 =
				sampleRate / static_cast<double>(params.hopSize);
			const double beatPeriodFrames =
				60.0 * framesPerSecond2 / detectedBpm;
			const int periodInt =
				std::max(1, static_cast<int>(std::round(beatPeriodFrames)));
			const int halfPeriod = periodInt / 2;

			for (int start = 0; start < numFrames; start += periodInt)
			{
				const int end    = std::min(start + periodInt, numFrames);
				int   bestFrame  = start;
				float bestVal    = onsetStrength[start];
				for (int f = start + 1; f < end; ++f)
				{
					if (onsetStrength[f] > bestVal)
					{
						bestVal   = onsetStrength[f];
						bestFrame = f;
					}
				}
				beatFrames.push_back(bestFrame);
			}

			std::sort(beatFrames.begin(), beatFrames.end());
			std::vector<int> refined;
			for (int i = 0; i < static_cast<int>(beatFrames.size()); ++i)
			{
				if (refined.empty() ||
				    beatFrames[i] - refined.back() >= halfPeriod)
					refined.push_back(beatFrames[i]);
			}
			beatFrames = refined;
		}
	}

	// ---- Build beat binary channel + beat phase ----
	std::vector<float> beatBinary(numFrames, 0.0f);
	std::vector<float> beatPhase(numFrames, 0.0f);

	for (int bf : beatFrames)
		if (bf >= 0 && bf < numFrames)
			beatBinary[bf] = 1.0f;

	if (beatFrames.size() >= 2)
	{
		for (int f = 0; f < beatFrames[0]; ++f)
			beatPhase[f] = 0.0f;

		for (size_t b = 0; b + 1 < beatFrames.size(); ++b)
		{
			const int start = beatFrames[b];
			const int end   = beatFrames[b + 1];
			const int span  = end - start;
			if (span <= 0) continue;
			for (int f = start; f < end; ++f)
				beatPhase[f] = static_cast<float>(f - start)
				             / static_cast<float>(span);
		}

		for (int f = beatFrames.back(); f < numFrames; ++f)
			beatPhase[f] = 0.0f;
	}

	progress.store(0.95f, std::memory_order_relaxed);

	// ---- Pack result: 6 channels ordered to match RT channel order ----
	// cache[0] = onset (binary), cache[1] = onset_strength,
	// cache[2] = bpm,            cache[3] = beat (binary),
	// cache[4] = beat_phase,     cache[5] = beat_confidence.
	result.cache.assign(kNumOutputChannels, std::vector<float>(numFrames, 0.0f));
	result.cache[0] = onsetBinary;
	result.cache[1] = onsetStrength;
	result.cache[2].assign(numFrames, detectedBpm);
	result.cache[3] = beatBinary;
	result.cache[4] = beatPhase;
	result.cache[5].assign(numFrames, beatConfidence);

	result.numFrames    = numFrames;
	result.sampleRate   = static_cast<float>(sampleRate / params.hopSize);
	result.detectedBpm  = detectedBpm;
	result.usedAutocorr = !rhythmExtractorOk;
	result.success      = true;

	progress.store(1.0f, std::memory_order_relaxed);
	return result;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

UNIFIED_CHOP_DLL_EXPORT(EssentiaRhythmCHOP, "Essentiarhythm", "Essentia Rhythm", "ESR")
