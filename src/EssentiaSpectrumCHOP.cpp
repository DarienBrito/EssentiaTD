// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaSpectrumCHOP.h"
#include "Shared/EssentiaInit.h"
#include "Shared/BatchCommon.h"

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
	myFrameProc.release();
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
	// Hop size has no effect in this CHOP — RT analysis is one frame per
	// cook over the latest fftSize samples (batch CHOPs have their own hop)
	inputs->enablePar(SpecHopsizeName, false);

	int fftSize = ParametersSpectrum::evalFftsize(inputs);
	if (fftSize <= 0) fftSize = 1024;

	int zeroPadFactor = ParametersSpectrum::evalZeropadding(inputs);
	int zeroPad = zeroPadFromFactor(zeroPadFactor, fftSize);

	// Real-input FFT is conjugate-symmetric, so only the non-negative
	// frequencies are unique: bin 0 = DC, last bin = Nyquist. The power of
	// two is the interval COUNT, hence /2 + 1 bins (1024 -> 513, never 512).
	int specBins = (fftSize + zeroPad) / 2 + 1;

	info->numChannels = 2;
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
	else if (index == 1)
		name->setString("phase");
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
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
		return;
	}

	// ---- Read parameters ----
	int fftSize = ParametersSpectrum::evalFftsize(inputs);
	int hopSize = ParametersSpectrum::evalHopsize(inputs);
	std::string winType = ParametersSpectrum::evalWindowtype(inputs);
	int zeroPadFactor = ParametersSpectrum::evalZeropadding(inputs);

	if (fftSize <= 0) fftSize = 1024;
	if (hopSize <= 0) hopSize = 512;

	int zeroPad = zeroPadFromFactor(zeroPadFactor, fftSize);

	// ---- Get input audio ----
	const OP_CHOPInput* audioIn = inputs->getInputCHOP(0);
	if (!audioIn || audioIn->numChannels < 1 || audioIn->numSamples < 1)
	{
		myError = "No audio input connected";
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
		return;
	}

	double sampleRate = audioIn->sampleRate;
	if (sampleRate <= 0) sampleRate = 44100.0;

	// ---- Reconfigure if parameters changed ----
	// sampleRate is intentionally excluded from this check — Windowing/FFT/
	// CartesianToPolar config depends only on fftSize/window/zeroPad, never
	// the audio rate. mySampleRate is tracked below solely for the info channel.
	if (fftSize != myFftSize ||
		winType != myWindowType ||
		zeroPad != myZeroPadding)
	{
		try
		{
			myFrameProc.configure(fftSize, winType, zeroPad);

			myFftSize = fftSize;
			myHopSize = hopSize;
			myZeroPadding = zeroPad;
			myWindowType = winType;
		}
		catch (const std::exception& e)
		{
			myError = std::string("Algorithm config failed: ") + e.what();
			myFrameProc.release();
		}
		catch (...)
		{
			myError = "Algorithm config failed with unknown error";
			myFrameProc.release();
		}
	}

	if (hopSize != myHopSize)
		myHopSize = hopSize;

	mySampleRate = sampleRate;

	// ---- Accumulate input audio and analyze the latest window ----
	// Input timeslices (~sampleRate/fps samples per cook) are shorter than
	// the FFT window; RTFrameProcessor assembles frames across cooks and
	// guards against double-writing a timeslice on forced re-cooks.
	myFrameProc.accumulate(audioIn->getChannelData(0),
	                       static_cast<size_t>(audioIn->numSamples),
	                       audioIn->totalCooks);

	if (!myFrameProc.processLatest())
		myWarning = myFrameProc.accumulatingWarning();

	// ---- Write output ----
	const int numSamp = output->numSamples;
	const std::vector<Real>& mag   = myFrameProc.magnitude();
	const std::vector<Real>& phase = myFrameProc.phase();

	for (int s = 0; s < numSamp; ++s)
		output->channels[0][s] = (s < (int)mag.size()) ? mag[s] : 0.0f;
	for (int s = 0; s < numSamp; ++s)
		output->channels[1][s] = (s < (int)phase.size()) ? phase[s] : 0.0f;
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
