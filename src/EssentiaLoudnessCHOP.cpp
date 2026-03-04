// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaLoudnessCHOP.h"
#include "Shared/EssentiaInit.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

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
// Construction / Destruction
// ---------------------------------------------------------------------------

EssentiaLoudnessCHOP::EssentiaLoudnessCHOP(const OP_NodeInfo* /*info*/)
{
	std::string initErr;
	myInitOk = ensureEssentiaInit(initErr);
	if (!myInitOk)
		myError = initErr;
}

EssentiaLoudnessCHOP::~EssentiaLoudnessCHOP()
{
	releaseAlgorithms();
}

// ---------------------------------------------------------------------------
// TD::CHOP_CPlusPlusBase overrides
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo,
                                          const OP_Inputs*, void*)
{
	ginfo->cookEveryFrame      = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->timeslice           = true;
	ginfo->inputMatchIndex     = -1;
}

bool EssentiaLoudnessCHOP::getOutputInfo(CHOP_OutputInfo* info,
                                         const OP_Inputs* inputs, void*)
{
	info->numChannels = kNumChannels;
	info->numSamples  = 1;
	double rate = inputs->getTimeInfo()->rate;
	if (rate <= 0.0) rate = 60.0;
	info->sampleRate  = static_cast<float>(rate);
	return true;
}

void EssentiaLoudnessCHOP::getChannelName(int32_t index, OP_String* name,
                                          const OP_Inputs*, void*)
{
	switch (index)
	{
	case 0: name->setString("loudness");           break;
	case 1: name->setString("loudness_momentary"); break;
	case 2: name->setString("loudness_shortterm"); break;
	case 3: name->setString("loudness_integrated");break;
	case 4: name->setString("dynamic_range");      break;
	case 5: name->setString("rms");                break;
	case 6: name->setString("zcr");                break;
	default: name->setString("unknown");           break;
	}
}

void EssentiaLoudnessCHOP::execute(CHOP_Output* output,
                                   const OP_Inputs* inputs, void*)
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

	// ---- Read parameters ----
	const int   frameSize     = ParametersLoudness::evalFramesize(inputs);
	const float gateThreshDb  = ParametersLoudness::evalGatethreshold(inputs);
	const bool  normalize     = ParametersLoudness::evalNormalize(inputs);
	const float dbFloor       = ParametersLoudness::evalDbfloor(inputs);

	// ---- Validate input ----
	const OP_CHOPInput* audioIn = inputs->getInputCHOP(0);
	if (!audioIn || audioIn->numChannels < 1 || audioIn->numSamples < 1)
	{
		myError = "No audio input connected";
		for (int ch = 0; ch < output->numChannels; ++ch)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[ch][s] = 0.0f;
		return;
	}

	double sampleRate = audioIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	// ---- Reconfigure when frame size or sample rate changes ----
	if (frameSize != myFrameSize || sampleRate != mySampleRate)
	{
		try
		{
			configureAlgorithms(frameSize);

			myFrameSize  = frameSize;
			mySampleRate = sampleRate;

			// Ring buffer: hold at least 4 frames worth of samples
			myAudioRing.resize(static_cast<size_t>(frameSize) * 4);
			myHopCounter = 0;

			myAudioFrame.resize(static_cast<size_t>(frameSize), 0.0f);

			// Compute window capacities in frames
			myMomentaryCapacity = std::max(1,
				static_cast<int>(std::ceil(kMomentaryWindowSec * sampleRate
				                           / static_cast<double>(frameSize))));

			myShortTermCapacity = std::max(1,
				static_cast<int>(std::ceil(kShortTermWindowSec * sampleRate
				                           / static_cast<double>(frameSize))));

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

	// ---- Accumulate input samples into ring buffer ----
	const float* audioData     = audioIn->getChannelData(0); // mono: first channel
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

	// ---- Prepare output values, applying normalization if enabled ----
	float outLoudness    = myLoudnessDb;
	float outMomentary   = myMomentaryLoudness;
	float outShortTerm   = myShortTermLoudness;
	float outIntegrated  = myIntegratedLoudness;
	float outDynRange    = myDynamicRange;

	if (normalize)
	{
		const float range = 0.0f - dbFloor; // always positive
		auto norm = [&](float dB) -> float {
			return std::clamp((dB - dbFloor) / range, 0.0f, 1.0f);
		};
		outLoudness   = norm(outLoudness);
		outMomentary  = norm(outMomentary);
		outShortTerm  = norm(outShortTerm);
		outIntegrated = norm(outIntegrated);
		// dynamic_range is a delta (max-min), not an absolute dB level
		outDynRange   = std::clamp(outDynRange / range, 0.0f, 1.0f);
	}

	// ---- Write output — repeat latest values for the whole time slice ----
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

void EssentiaLoudnessCHOP::setupParameters(OP_ParameterManager* manager, void*)
{
	ParametersLoudness::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP
// ---------------------------------------------------------------------------

int32_t EssentiaLoudnessCHOP::getNumInfoCHOPChans(void*)
{
	return 3;
}

void EssentiaLoudnessCHOP::getInfoCHOPChan(int32_t index,
                                           OP_InfoCHOPChan* chan, void*)
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
		chan->name->setString("buffer_fill");
		chan->value = static_cast<float>(myAudioRing.available());
		break;
	default:
		break;
	}
}

void EssentiaLoudnessCHOP::getWarningString(OP_String* warning, void* /*reserved1*/)
{
	if (!myWarning.empty())
		warning->setString(myWarning.c_str());
}

void EssentiaLoudnessCHOP::getErrorString(OP_String* error, void* /*reserved1*/)
{
	if (!myError.empty())
		error->setString(myError.c_str());
}

// ---------------------------------------------------------------------------
// Algorithm management
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::configureAlgorithms(int /*frameSize*/)
{
	releaseAlgorithms();

	// Essentia Loudness: input "signal" (vector<Real>), output "loudness" (Real)
	// No required construction parameters.
	myLoudnessAlgo = AlgorithmFactory::create("Loudness");
}

void EssentiaLoudnessCHOP::releaseAlgorithms()
{
	delete myLoudnessAlgo;
	myLoudnessAlgo = nullptr;
}

// ---------------------------------------------------------------------------
// Per-frame processing
// ---------------------------------------------------------------------------

void EssentiaLoudnessCHOP::processFrame()
{
	// Read the latest frameSize samples from the ring buffer
	myAudioRing.readLatest(myAudioFrame, static_cast<size_t>(myFrameSize));

	// Compute RMS from raw audio frame
	{
		double sum = 0.0;
		for (int i = 0; i < myFrameSize; ++i)
			sum += static_cast<double>(myAudioFrame[i]) * static_cast<double>(myAudioFrame[i]);
		myRms = static_cast<float>(std::sqrt(sum / myFrameSize));
	}

	// Compute ZCR (zero crossing rate) from raw audio frame
	{
		int crossings = 0;
		for (int i = 1; i < myFrameSize; ++i)
		{
			if ((myAudioFrame[i] >= 0.0f) != (myAudioFrame[i - 1] >= 0.0f))
				++crossings;
		}
		myZcr = static_cast<float>(crossings) / static_cast<float>(myFrameSize - 1);
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
	// Essentia's Loudness returns mean(x^2) — i.e. mean-square power.
	// We map it to dBFS-like: 10 * log10(power + 1e-10).
	const float power = static_cast<float>(myEssentiaLoudness);
	myLoudnessDb = 10.0f * std::log10(power + 1e-10f);

	// ---- Push into momentary window ----
	myMomentaryWindow.push_back(myLoudnessDb);
	if (static_cast<int>(myMomentaryWindow.size()) > myMomentaryCapacity)
		myMomentaryWindow.pop_front();

	// ---- Push into short-term window ----
	myShortTermWindow.push_back(myLoudnessDb);
	if (static_cast<int>(myShortTermWindow.size()) > myShortTermCapacity)
		myShortTermWindow.pop_front();

	// ---- Update windowed loudness outputs ----
	myMomentaryLoudness = windowedLoudnessDb(myMomentaryWindow);
	myShortTermLoudness = windowedLoudnessDb(myShortTermWindow);

	// ---- Accumulate short-term value for integrated loudness computation ----
	// Cap at ~1 hour of data (~156k entries) to prevent unbounded growth
	static constexpr size_t kMaxIntegratedEntries = 200000;
	if (myIntegratedValues.size() >= kMaxIntegratedEntries)
		myIntegratedValues.erase(myIntegratedValues.begin(),
		                         myIntegratedValues.begin() + (myIntegratedValues.size() / 4));
	myIntegratedValues.push_back(myShortTermLoudness);

	// ---- Dynamic range: max - min of the short-term window ----
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

	// Convert each dB value back to linear power, average, convert back to dB.
	// power = 10^(db/10)
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

	// --- Pass 1: absolute gate ---
	// Collect all short-term values above the absolute gate threshold.
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

	// Compute ungated mean of pass-1 values (in power domain).
	double sumPower = 0.0;
	for (const float v : pass1)
		sumPower += std::pow(10.0, static_cast<double>(v) / 10.0);
	const double ungatedMeanPower = sumPower / static_cast<double>(pass1.size());
	const float  ungatedMeanDb    =
		static_cast<float>(10.0 * std::log10(ungatedMeanPower + 1e-30));

	// --- Pass 2: relative gate — keep values >= ungatedMean - 10 LU ---
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
	ci.opType->setString("Essentialoudness");
	ci.opLabel->setString("Essentia Loudness");
	ci.opIcon->setString("ESL");
	ci.authorName->setString("Darien Brito");
	ci.authorEmail->setString("info@darienbrito.com");
	ci.minInputs = 1;
	ci.maxInputs = 1;
}

DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
	return new EssentiaLoudnessCHOP(info);
}

DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
{
	delete static_cast<EssentiaLoudnessCHOP*>(instance);
}

} // extern "C"
