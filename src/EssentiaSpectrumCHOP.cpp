// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaSpectrumCHOP.h"
#include "Shared/EssentiaInit.h"

#include <cmath>
#include <cstring>
#include <algorithm>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

EssentiaSpectrumCHOP::EssentiaSpectrumCHOP(const OP_NodeInfo* /*info*/)
{
	std::string initErr;
	myInitOk = ensureEssentiaInit(initErr);
	if (!myInitOk)
		myError = initErr;
}

EssentiaSpectrumCHOP::~EssentiaSpectrumCHOP()
{
	releaseAlgorithms();
}

// ---------------------------------------------------------------------------
// TD overrides
// ---------------------------------------------------------------------------

void EssentiaSpectrumCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo, const OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->timeslice = false;
	ginfo->inputMatchIndex = -1;
}

bool EssentiaSpectrumCHOP::getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, void*)
{
	int fftSize = ParametersSpectrum::evalFftsize(inputs);
	if (fftSize <= 0) fftSize = 1024;

	int zeroPadFactor = ParametersSpectrum::evalZeropadding(inputs);
	int zeroPad = 0;
	if (zeroPadFactor == 1) zeroPad = fftSize / 2;
	else if (zeroPadFactor == 2) zeroPad = fftSize;

	int specBins = (fftSize + zeroPad) / 2 + 1;

	info->numChannels = 1;
	info->numSamples = specBins;
	info->startIndex = 0;
	info->sampleRate = static_cast<float>(
		inputs->getInputCHOP(0) ? inputs->getInputCHOP(0)->sampleRate : 44100.0f);
	return true;
}

void EssentiaSpectrumCHOP::getChannelName(int32_t index, OP_String* name,
	const OP_Inputs*, void*)
{
	if (index == 0)
		name->setString("spectrum");
	else
		name->setString("unknown");
}

void EssentiaSpectrumCHOP::execute(CHOP_Output* output, const OP_Inputs* inputs, void*)
{
	myError.clear();
	myWarning.clear();

	// ---- Guard: Essentia must be initialized ----
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		for (int s = 0; s < output->numSamples; ++s)
			output->channels[0][s] = 0.0f;
		return;
	}

	// ---- Read parameters ----
	int fftSize = ParametersSpectrum::evalFftsize(inputs);
	int hopSize = ParametersSpectrum::evalHopsize(inputs);
	int winTypeIdx = ParametersSpectrum::evalWindowtype(inputs);
	int zeroPadFactor = ParametersSpectrum::evalZeropadding(inputs);

	if (fftSize <= 0) fftSize = 1024;
	if (hopSize <= 0) hopSize = 512;

	static const char* windowNames[] = {
		"hann", "hamming", "triangular",
		"blackmanharris62", "blackmanharris70",
		"blackmanharris74", "blackmanharris92"
	};
	winTypeIdx = std::clamp(winTypeIdx, 0, 6);
	const char* winType = windowNames[winTypeIdx];

	// Compute actual zero-padding amount from factor
	int zeroPad = 0;
	if (zeroPadFactor == 1) zeroPad = fftSize / 2;
	else if (zeroPadFactor == 2) zeroPad = fftSize;

	// ---- Get input audio ----
	const OP_CHOPInput* audioIn = inputs->getInputCHOP(0);
	if (!audioIn || audioIn->numChannels < 1 || audioIn->numSamples < 1)
	{
		myError = "No audio input connected";
		for (int s = 0; s < output->numSamples; ++s)
			output->channels[0][s] = 0.0f;
		return;
	}

	double sampleRate = audioIn->sampleRate;
	if (sampleRate <= 0) sampleRate = 44100.0;

	// ---- Reconfigure if parameters changed ----
	if (fftSize != myFftSize ||
		std::string(winType) != myWindowType || sampleRate != mySampleRate ||
		zeroPad != myZeroPadding)
	{
		try
		{
			configureAlgorithms(fftSize, winType, zeroPad);

			myFftSize = fftSize;
			myHopSize = hopSize;
			myZeroPadding = zeroPad;
			myWindowType = winType;
			mySampleRate = sampleRate;
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

	if (hopSize != myHopSize)
		myHopSize = hopSize;

	// ---- Read latest fftSize samples from input ----
	const float* audioData = audioIn->getChannelData(0);
	int totalInputSamples = audioIn->numSamples;

	if (totalInputSamples >= myFftSize)
	{
		int offset = totalInputSamples - myFftSize;
		for (int i = 0; i < myFftSize; ++i)
			myAudioFrame[i] = audioData[offset + i];
		processFrame();
	}
	else if (totalInputSamples > 0)
	{
		// Not enough samples yet — zero-pad the beginning
		std::fill(myAudioFrame.begin(), myAudioFrame.end(), 0.0f);
		int destOffset = myFftSize - totalInputSamples;
		for (int i = 0; i < totalInputSamples; ++i)
			myAudioFrame[destOffset + i] = audioData[i];
		processFrame();
	}

	// ---- Write output ----
	const int numSamp = output->numSamples;

	for (int s = 0; s < numSamp; ++s)
		output->channels[0][s] = (s < (int)mySpectrumMag.size()) ? mySpectrumMag[s] : 0.0f;
}

void EssentiaSpectrumCHOP::setupParameters(OP_ParameterManager* manager, void*)
{
	ParametersSpectrum::setup(manager);
}

int32_t EssentiaSpectrumCHOP::getNumInfoCHOPChans(void*)
{
	return 3;
}

void EssentiaSpectrumCHOP::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void*)
{
	switch (index)
	{
	case 0:
		chan->name->setString("fft_size");
		chan->value = (float)myFftSize;
		break;
	case 1:
		chan->name->setString("hop_size");
		chan->value = (float)myHopSize;
		break;
	case 2:
		chan->name->setString("sample_rate");
		chan->value = (float)mySampleRate;
		break;
	}
}

void EssentiaSpectrumCHOP::getWarningString(OP_String* warning, void* /*reserved1*/)
{
	if (!myWarning.empty())
		warning->setString(myWarning.c_str());
}

void EssentiaSpectrumCHOP::getErrorString(OP_String* error, void* /*reserved1*/)
{
	if (!myError.empty())
		error->setString(myError.c_str());
}

// ---------------------------------------------------------------------------
// Algorithm management
// ---------------------------------------------------------------------------

void EssentiaSpectrumCHOP::configureAlgorithms(int fftSize, const char* windowType, int zeroPadding)
{
	releaseAlgorithms();

	const int paddedSize = fftSize + zeroPadding;
	int specBins = paddedSize / 2 + 1;

	myAudioFrame.resize(fftSize, 0.0f);
	myWindowedFrame.resize(paddedSize, 0.0f);
	mySpectrumMag.resize(specBins, 0.0f);

	myWindowing = AlgorithmFactory::create("Windowing",
		"type", std::string(windowType),
		"size", fftSize,
		"zeroPadding", zeroPadding,
		"normalized", true);

	mySpectrum = AlgorithmFactory::create("Spectrum",
		"size", paddedSize);
}

void EssentiaSpectrumCHOP::releaseAlgorithms()
{
	delete myWindowing;  myWindowing = nullptr;
	delete mySpectrum;   mySpectrum = nullptr;
}

void EssentiaSpectrumCHOP::processFrame()
{
	try
	{
		if (myWindowing)
		{
			myWindowing->input("frame").set(myAudioFrame);
			myWindowing->output("frame").set(myWindowedFrame);
			myWindowing->compute();
		}

		if (mySpectrum)
		{
			mySpectrum->input("frame").set(myWindowedFrame);
			mySpectrum->output("spectrum").set(mySpectrumMag);
			mySpectrum->compute();
		}
	}
	catch (...)
	{
		std::fill(mySpectrumMag.begin(), mySpectrumMag.end(), 0.0f);
	}
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
	ci.opType->setString("Essentiaspectrum");
	ci.opLabel->setString("Essentia Spectrum");
	ci.opIcon->setString("ESS");
	ci.authorName->setString("Darien Brito");
	ci.authorEmail->setString("info@darienbrito.com");
	ci.minInputs = 1;
	ci.maxInputs = 1;
}

DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
	return new EssentiaSpectrumCHOP(info);
}

DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
{
	delete static_cast<EssentiaSpectrumCHOP*>(instance);
}

} // extern "C"
