// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaRhythmCHOP.h"
#include "Shared/EssentiaInit.h"
#include "Shared/Utils.h"

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

/// Onset strength history is stored at one entry per execute() call.
/// kOnsetHistorySize slots cover ~8 s at a 60 fps cook rate.
static constexpr int   kNumOutputChannels   = 6;
static constexpr float kBpmSmoothingAlpha   = 0.10f; ///< EMA coefficient for BPM
static constexpr float kMinAutocorrPeak     = 1e-6f; ///< avoid divide-by-zero

// Channel name table (index must match output channel order in execute())
static const char* const kChannelNames[kNumOutputChannels] = {
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

EssentiaRhythmCHOP::EssentiaRhythmCHOP(const OP_NodeInfo* /*info*/)
{
	std::string initErr;
	myInitOk = ensureEssentiaInit(initErr);
	if (!myInitOk)
		myError = initErr;

	myOnsetHistory.resize(kOnsetHistorySize, 0.0f);
}

EssentiaRhythmCHOP::~EssentiaRhythmCHOP()
{
	releaseAlgorithms();
}

// ---------------------------------------------------------------------------
// TD overrides
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo,
                                         const OP_Inputs* /*inputs*/,
                                         void* /*reserved*/)
{
	ginfo->cookEveryFrame     = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->timeslice          = false;
	ginfo->inputMatchIndex    = -1;
}

bool EssentiaRhythmCHOP::getOutputInfo(CHOP_OutputInfo* info,
                                        const OP_Inputs* inputs,
                                        void* /*reserved*/)
{
	info->numChannels = kNumOutputChannels;
	info->numSamples  = 1;
	info->sampleRate  = static_cast<float>(inputs->getTimeInfo()->rate);

	// Show Bias Center only when Tempo Bias toggle is ON
	const bool tempoBias = ParametersRhythm::evalTempobias(inputs);
	inputs->enablePar(BiascenterName, tempoBias);

	return true;
}

void EssentiaRhythmCHOP::getChannelName(int32_t index, OP_String* name,
                                         const OP_Inputs* /*inputs*/,
                                         void* /*reserved*/)
{
	if (index >= 0 && index < kNumOutputChannels)
		name->setString(kChannelNames[index]);
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------
// execute — main cook function
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::execute(CHOP_Output* output,
                                  const OP_Inputs* inputs,
                                  void* /*reserved*/)
{
	myError.clear();
	myWarning.clear();

	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
		return;
	}

	// Reset per-frame trigger outputs before any early-return
	myOutOnset = 0.0f;
	myOutBeat  = 0.0f;

	// ---- Read parameters ----
	const int   onsetMethodIdx  = ParametersRhythm::evalOnsetmethod(inputs);
	const float sensitivity     = ParametersRhythm::evalOnsetsensitivity(inputs);
	const int   bpmMin          = ParametersRhythm::evalBpmmin(inputs);
	int         bpmMax          = ParametersRhythm::evalBpmmax(inputs);
	if (bpmMax <= bpmMin) bpmMax = bpmMin + 1;
	const bool  tempoBias       = ParametersRhythm::evalTempobias(inputs);
	const float biasCenter      = ParametersRhythm::evalBiascenter(inputs);

	static const char* kMethodNames[] = { "hfc", "complex", "flux" };
	const int clampedMethodIdx = std::clamp(onsetMethodIdx, 0, 2);
	const char* onsetMethod = kMethodNames[clampedMethodIdx];

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

	// ---- Extract spectrum magnitude from input channel ----
	std::vector<float> specMagFloat;
	const bool hasSpectrum = extractChannelSamples(chopIn, "spectrum", specMagFloat);

	if (!hasSpectrum || specMagFloat.empty())
	{
		myWarning = "No spectrum channel found in input — connect EssentiaSpectrumCHOP";
		// Still update beat phase with existing BPM
		updateBeatPhase(bpmMin, bpmMax);
		writeOutputs(output);
		return;
	}

	const int specSize = (int)specMagFloat.size();

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
	// We derive hopSize from the cook rate because EssentiaCoreCHOP's hop is
	// not directly exposed here.  A reasonable approximation:
	//   hopSize = sampleRate / fps
	// We keep a running fps estimate updated each frame.
	++myFrameCount;
	// Use the input's sample-rate context.  Default to 44100/512 ≈ 86 fps.
	// We will refine this if TD supplies a meaningful sampleRate on the CHOP.
	// For autocorrelation lag-to-BPM conversion we only need an approximate hop.
	const int approxHopSize = static_cast<int>(sampleRate / myFpsEstimate);
	myHopSize = (approxHopSize > 0) ? approxHopSize : 512;

	// ---- Run OnsetDetection ----
	// Convert float spectrum to essentia::Real
	mySpectrumBuf.assign(specMagFloat.begin(), specMagFloat.end());

	// Phase approximation: use zeros (we only have magnitude from Core)
	myPhaseBuf.assign(specSize, 0.0f);

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

	myOutOnsetStrength = (float)myOnsetValue;

	// ---- Onset trigger ----
	// Map sensitivity [0,1] to a threshold.  Sensitivity=0 → very high threshold
	// (rarely triggers); sensitivity=1 → very low threshold (triggers often).
	// OnsetDetection values are unbounded; we use an adaptive approach:
	// compute a running maximum and scale relative to it.
	myRunningMax = std::max(myRunningMax * 0.999f, myOutOnsetStrength);

	const float normalised = (myRunningMax > kMinAutocorrPeak)
	                         ? (myOutOnsetStrength / myRunningMax)
	                         : 0.0f;

	// threshold decreases as sensitivity increases
	const float threshold = 1.0f - sensitivity;
	myOutOnset = (normalised >= threshold) ? 1.0f : 0.0f;

	// ---- Push onset strength into circular history buffer ----
	pushOnsetStrength(myOutOnsetStrength);

	// ---- Compute autocorrelation BPM ----
	const float rawBpm = computeAutocorrBpm(bpmMin, bpmMax, sampleRate, myHopSize,
	                                         tempoBias, biasCenter);
	if (rawBpm > 0.0f)
	{
		// Push into median buffer
		myBpmMedianBuf[myBpmMedianPos] = rawBpm;
		myBpmMedianPos = (myBpmMedianPos + 1) % kBpmMedianSize;
		if (myBpmMedianFill < kBpmMedianSize)
			++myBpmMedianFill;

		// Compute median of filled portion
		std::array<float, kBpmMedianSize> sorted;
		for (int i = 0; i < myBpmMedianFill; ++i)
			sorted[i] = myBpmMedianBuf[i];
		std::sort(sorted.begin(), sorted.begin() + myBpmMedianFill);
		const float medianBpm = sorted[myBpmMedianFill / 2];

		// Exponential moving average on median-filtered value
		mySmoothedBpm = mySmoothedBpm * (1.0f - kBpmSmoothingAlpha)
		                + medianBpm * kBpmSmoothingAlpha;
		// Clamp to user range
		mySmoothedBpm = std::clamp(mySmoothedBpm,
		                           (float)bpmMin, (float)bpmMax);
	}
	myOutBpm = mySmoothedBpm;

	// ---- Beat phase & beat trigger ----
	updateBeatPhase(bpmMin, bpmMax);

	// ---- Commit outputs ----
	writeOutputs(output);
}

// ---------------------------------------------------------------------------
// Parameter setup
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::setupParameters(OP_ParameterManager* manager, void* /*reserved*/)
{
	ParametersRhythm::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP channels
// ---------------------------------------------------------------------------

int32_t EssentiaRhythmCHOP::getNumInfoCHOPChans(void* /*reserved*/)
{
	return 2;
}

void EssentiaRhythmCHOP::getInfoCHOPChan(int32_t index,
                                           OP_InfoCHOPChan* chan,
                                           void* /*reserved*/)
{
	switch (index)
	{
	case 0:
		chan->name->setString("onset_buffer_fill");
		chan->value = (float)myOnsetFillCount;
		break;
	case 1:
		chan->name->setString("current_bpm");
		chan->value = mySmoothedBpm;
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Warning / error strings
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::getWarningString(OP_String* warning, void* /*reserved1*/)
{
	if (!myWarning.empty())
		warning->setString(myWarning.c_str());
}

void EssentiaRhythmCHOP::getErrorString(OP_String* error, void* /*reserved1*/)
{
	if (!myError.empty())
		error->setString(myError.c_str());
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::configureOnsetDetection(int specSize,
                                                   const char* method,
                                                   double sampleRate)
{
	releaseAlgorithms();

	mySpectrumBuf.assign(specSize, 0.0f);
	myPhaseBuf.assign(specSize, 0.0f);

	// Reset median BPM buffer on reconfigure
	myBpmMedianBuf.fill(0.0f);
	myBpmMedianPos  = 0;
	myBpmMedianFill = 0;

	myOnsetDetection = AlgorithmFactory::create("OnsetDetection",
		"method",     std::string(method),
		"sampleRate", (Real)sampleRate);
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
	                            ? (int)(sampleRate / (double)hopSize)
	                            : 60;

	const int lagMin = (int)(60.0 * framesPerSecond / (double)bpmMax);
	const int lagMax = (int)(60.0 * framesPerSecond / (double)bpmMin);

	if (lagMin < 1 || lagMax < lagMin)
		return 0.0f;

	// Need enough history for harmonic summation: 4 * lagMax
	if (myOnsetFillCount < lagMax * 4 + 1)
	{
		// Fall back to basic autocorrelation if we have at least lagMax+1
		if (myOnsetFillCount < lagMax + 1)
			return 0.0f;
	}

	// Build a contiguous view of the circular buffer (oldest first)
	const int histLen = std::min(myOnsetFillCount, kOnsetHistorySize);
	std::vector<float> buf(histLen);
	{
		int readStart;
		if (myOnsetFillCount < kOnsetHistorySize)
			readStart = 0;
		else
			readStart = myOnsetWritePos;

		for (int i = 0; i < histLen; ++i)
			buf[i] = myOnsetHistory[(readStart + i) % kOnsetHistorySize];
	}

	// Compute mean for zero-centering
	const float mean = std::accumulate(buf.begin(), buf.end(), 0.0f)
	                   / (float)histLen;

	// Zero-lag energy (R(0))
	double r0 = 0.0;
	for (int i = 0; i < histLen; ++i)
	{
		double v = (double)(buf[i] - mean);
		r0 += v * v;
	}
	if (r0 < (double)kMinAutocorrPeak)
		return 0.0f;

	// Pre-compute autocorrelation for all lags we might need (up to 4 * lagMax)
	const int maxNeededLag = std::min(lagMax * 4, histLen - 1);
	std::vector<double> autocorr(maxNeededLag + 1, 0.0);
	for (int lag = lagMin; lag <= maxNeededLag; ++lag)
	{
		double r = 0.0;
		int    n = 0;
		for (int i = 0; i + lag < histLen; ++i)
		{
			r += (double)(buf[i] - mean) * (double)(buf[i + lag] - mean);
			++n;
		}
		if (n > 0)
			autocorr[lag] = r / (double)n;
	}

	// Harmonic summation scoring: for each candidate lag, sum autocorrelation
	// at harmonics h=1..4, weighted by 1/h
	int    bestLag   = lagMin;
	double bestScore = -1e9;

	static constexpr int    kNumHarmonics = 4;
	static constexpr double kBiasSigma    = 40.0; // BPM standard deviation for Gaussian prior

	for (int lag = lagMin; lag <= lagMax; ++lag)
	{
		double score = 0.0;
		for (int h = 1; h <= kNumHarmonics; ++h)
		{
			const int hLag = h * lag;
			if (hLag <= maxNeededLag)
				score += autocorr[hLag] / (double)h;
		}

		// Optional Gaussian tempo prior
		if (tempoBias && lag > 0)
		{
			const double bpm = 60.0 * framesPerSecond / (double)lag;
			const double diff = (bpm - (double)biasCenter) / kBiasSigma;
			score *= std::exp(-0.5 * diff * diff);
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestLag   = lag;
		}
	}

	// Update confidence: best harmonic score normalised by zero-lag power
	const float confidence = (float)std::clamp(bestScore / (r0 / (double)histLen),
	                                            0.0, 1.0);
	myBeatConfidence = confidence;

	if (bestLag <= 0)
		return 0.0f;

	const float bpm = 60.0f * (float)framesPerSecond / (float)bestLag;
	return bpm;
}

void EssentiaRhythmCHOP::updateBeatPhase(int bpmMin, int bpmMax)
{
	const float clampedBpm = std::clamp(mySmoothedBpm,
	                                    (float)bpmMin, (float)bpmMax);

	// Phase increment per frame: bpm beats/min / (fps frames/s * 60 s/min)
	const float phaseIncrement = clampedBpm / (float)(myFpsEstimate * 60.0);

	myBeatPhase += phaseIncrement;

	if (myBeatPhase >= 1.0f)
	{
		myBeatPhase  = std::fmod(myBeatPhase, 1.0f);
		myOutBeat    = 1.0f;
	}

	myOutBpm           = clampedBpm;
	myOutBeatPhase     = myBeatPhase;
	myOutBeatConfidence = myBeatConfidence;
}

void EssentiaRhythmCHOP::writeOutputs(CHOP_Output* output)
{
	// Guard: output must have 6 channels with 1 sample each
	if (!output || output->numChannels < kNumOutputChannels)
		return;

	output->channels[0][0] = myOutOnset;
	output->channels[1][0] = myOutOnsetStrength;
	output->channels[2][0] = myOutBpm;
	output->channels[3][0] = myOutBeat;
	output->channels[4][0] = myOutBeatPhase;
	output->channels[5][0] = myOutBeatConfidence;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

using namespace EssentiaTD;

extern "C"
{

DLLEXPORT void FillCHOPPluginInfo(CHOP_PluginInfo* info)
{
	info->apiVersion = CHOPCPlusPlusAPIVersion;
	OP_CustomOPInfo& ci = info->customOPInfo;
	ci.opType->setString("Essentiarhythm");
	ci.opLabel->setString("Essentia Rhythm");
	ci.opIcon->setString("ESR");
	ci.authorName->setString("Darien Brito");
	ci.authorEmail->setString("info@darienbrito.com");
	ci.minInputs = 1;
	ci.maxInputs = 1;
}

DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
	return new EssentiaRhythmCHOP(info);
}

DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
{
	delete static_cast<EssentiaRhythmCHOP*>(instance);
}

} // extern "C"
