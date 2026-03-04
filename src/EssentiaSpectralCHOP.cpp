// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaSpectralCHOP.h"
#include "Shared/EssentiaInit.h"
#include "Shared/Utils.h"

#include <algorithm>
#include <cstring>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ===========================================================================
// Construction / Destruction
// ===========================================================================

EssentiaSpectralCHOP::EssentiaSpectralCHOP(const OP_NodeInfo* /*info*/)
{
	std::string initErr;
	myInitOk = ensureEssentiaInit(initErr);
	if (!myInitOk)
		myError = initErr;

	// Pre-size contrast buffers to their fixed sizes so they are never empty
	myContrastBands.resize(kContrastBands, 0.0f);
	myContrastValleys.resize(kContrastBands, 0.0f);
}

EssentiaSpectralCHOP::~EssentiaSpectralCHOP()
{
	releaseAlgorithms();
}

// ===========================================================================
// TD overrides
// ===========================================================================

void EssentiaSpectralCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo,
                                           const OP_Inputs* inputs,
                                           void*)
{
	ginfo->cookEveryFrame      = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->timeslice           = false;
	ginfo->inputMatchIndex     = -1;
}

bool EssentiaSpectralCHOP::getOutputInfo(CHOP_OutputInfo* info,
                                          const OP_Inputs* inputs,
                                          void*)
{
	bool enableMfcc       = ParametersSpectral::evalEnablemfcc(inputs);
	int  mfccCount        = ParametersSpectral::evalMfcccount(inputs);
	bool enableCentroid   = ParametersSpectral::evalEnablecentroid(inputs);
	bool enableFlux       = ParametersSpectral::evalEnableflux(inputs);
	bool enableRolloff    = ParametersSpectral::evalEnablerolloff(inputs);
	bool enableContrast   = ParametersSpectral::evalEnablecontrast(inputs);
	bool enableHfc        = ParametersSpectral::evalEnablehfc(inputs);
	bool enableComplexity = ParametersSpectral::evalEnablecomplexity(inputs);
	bool enableMel        = ParametersSpectral::evalEnablemel(inputs);
	int  melBandCount     = ParametersSpectral::evalMelbandscount(inputs);

	// Parameter co-dependencies
	inputs->enablePar("Mfcccount", enableMfcc);
	inputs->enablePar("Melbandscount", enableMel);

	// Count output channels
	int numCh = 0;
	if (enableMfcc)       numCh += mfccCount;
	if (enableCentroid)   numCh += 1;
	if (enableFlux)       numCh += 1;
	if (enableRolloff)    numCh += 1;
	if (enableContrast)   numCh += kContrastBands;
	if (enableHfc)        numCh += 1;
	if (enableComplexity) numCh += 1;
	if (enableMel)        numCh += melBandCount;

	info->numChannels = (numCh > 0) ? numCh : 1; // always at least 1 channel
	info->numSamples  = 1;
	info->sampleRate  = static_cast<float>(inputs->getTimeInfo()->rate);
	return true;
}

void EssentiaSpectralCHOP::getChannelName(int32_t index,
                                           OP_String* name,
                                           const OP_Inputs*,
                                           void*)
{
	if (index >= 0 && index < static_cast<int32_t>(myChannelNames.size()))
		name->setString(myChannelNames[static_cast<size_t>(index)].c_str());
	else
		name->setString("unknown");
}

void EssentiaSpectralCHOP::execute(CHOP_Output* output,
                                    const OP_Inputs* inputs,
                                    void*)
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
	const bool enableMfcc       = ParametersSpectral::evalEnablemfcc(inputs);
	const int  mfccCount        = ParametersSpectral::evalMfcccount(inputs);
	const bool enableCentroid   = ParametersSpectral::evalEnablecentroid(inputs);
	const bool enableFlux       = ParametersSpectral::evalEnableflux(inputs);
	const bool enableRolloff    = ParametersSpectral::evalEnablerolloff(inputs);
	const bool enableContrast   = ParametersSpectral::evalEnablecontrast(inputs);
	const bool enableHfc        = ParametersSpectral::evalEnablehfc(inputs);
	const bool enableComplexity = ParametersSpectral::evalEnablecomplexity(inputs);
	const bool enableMel        = ParametersSpectral::evalEnablemel(inputs);
	const int  melBandCount     = ParametersSpectral::evalMelbandscount(inputs);

	// ---- Validate input ----
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1)
	{
		myError = "No input connected — connect EssentiaCoreCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	// ---- Extract spectrum from input ----
	std::vector<float> spectrumF;
	if (!extractChannelSamples(chopIn, "spectrum", spectrumF) || spectrumF.empty())
	{
		myError = "Input has no spectrum channel — connect EssentiaSpectrumCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	const int  specBins    = static_cast<int>(spectrumF.size());
	const double sampleRate = (chopIn->sampleRate > 0.0) ? chopIn->sampleRate : 44100.0;

	// ---- Detect configuration change ----
	const bool featureFlagsChanged =
		(enableMfcc       != myPrevEnableMfcc)    ||
		(enableCentroid   != myPrevEnableCentroid) ||
		(enableFlux       != myPrevEnableFlux)     ||
		(enableRolloff    != myPrevEnableRolloff)  ||
		(enableContrast   != myPrevEnableContrast) ||
		(enableHfc        != myPrevEnableHfc)      ||
		(enableComplexity != myPrevEnableComplexity)||
		(mfccCount        != myPrevMfccCount)      ||
		(enableMel        != myPrevEnableMel)      ||
		(melBandCount     != myPrevMelBandCount);

	const bool configChanged =
		(specBins     != mySpecBins)      ||
		(mfccCount    != myMfccCount)     ||
		(melBandCount != myMelBandCount)  ||
		(sampleRate   != mySampleRate)    ||
		featureFlagsChanged;

	if (configChanged)
	{
		try
		{
			configureAlgorithms(specBins, mfccCount, melBandCount, sampleRate);

			mySpecBins     = specBins;
			myMfccCount    = mfccCount;
			myMelBandCount = melBandCount;
			mySampleRate   = sampleRate;

			myPrevEnableMfcc       = enableMfcc;
			myPrevEnableCentroid   = enableCentroid;
			myPrevEnableFlux       = enableFlux;
			myPrevEnableRolloff    = enableRolloff;
			myPrevEnableContrast   = enableContrast;
			myPrevEnableHfc        = enableHfc;
			myPrevEnableComplexity = enableComplexity;
			myPrevMfccCount        = mfccCount;
			myPrevEnableMel        = enableMel;
			myPrevMelBandCount     = melBandCount;
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
		rebuildChannelNames(enableMfcc, mfccCount,
		                    enableCentroid, enableFlux, enableRolloff,
		                    enableContrast, enableHfc, enableComplexity,
		                    enableMel, melBandCount);
	}

	// ---- Run algorithms ----
	processFrame(spectrumF,
	             enableMfcc,   mfccCount,
	             enableCentroid,
	             enableFlux,
	             enableRolloff,
	             enableContrast,
	             enableHfc,
	             enableComplexity,
	             enableMel,    melBandCount);

	// ---- Write output channels ----
	int ch = 0;

	if (enableMfcc)
	{
		for (int i = 0; i < mfccCount; ++i)
		{
			const float val = (i < static_cast<int>(myMfccCoeffs.size()))
			                  ? static_cast<float>(myMfccCoeffs[static_cast<size_t>(i)])
			                  : 0.0f;
			if (ch < output->numChannels)
				output->channels[ch][0] = val;
			++ch;
		}
	}

	if (enableCentroid)
	{
		if (ch < output->numChannels)
			output->channels[ch][0] = static_cast<float>(myCentroidVal);
		++ch;
	}

	if (enableFlux)
	{
		if (ch < output->numChannels)
			output->channels[ch][0] = static_cast<float>(myFluxVal);
		++ch;
	}

	if (enableRolloff)
	{
		if (ch < output->numChannels)
			output->channels[ch][0] = static_cast<float>(myRollOffVal);
		++ch;
	}

	if (enableContrast)
	{
		for (int i = 0; i < kContrastBands; ++i)
		{
			const float val = (i < static_cast<int>(myContrastBands.size()))
			                  ? static_cast<float>(myContrastBands[static_cast<size_t>(i)])
			                  : 0.0f;
			if (ch < output->numChannels)
				output->channels[ch][0] = val;
			++ch;
		}
	}

	if (enableHfc)
	{
		if (ch < output->numChannels)
			output->channels[ch][0] = static_cast<float>(myHfcVal);
		++ch;
	}

	if (enableComplexity)
	{
		if (ch < output->numChannels)
			output->channels[ch][0] = static_cast<float>(myComplexityVal);
		++ch;
	}

	if (enableMel)
	{
		for (int i = 0; i < melBandCount; ++i)
		{
			const float val = (i < static_cast<int>(myMelBandValues.size()))
			                  ? static_cast<float>(myMelBandValues[static_cast<size_t>(i)])
			                  : 0.0f;
			if (ch < output->numChannels)
				output->channels[ch][0] = val;
			++ch;
		}
	}
}

void EssentiaSpectralCHOP::setupParameters(OP_ParameterManager* manager, void*)
{
	ParametersSpectral::setup(manager);
}

// ===========================================================================
// Info CHOP
// ===========================================================================

int32_t EssentiaSpectralCHOP::getNumInfoCHOPChans(void*)
{
	return 2;
}

void EssentiaSpectralCHOP::getInfoCHOPChan(int32_t index,
                                             OP_InfoCHOPChan* chan,
                                             void*)
{
	switch (index)
	{
	case 0:
		chan->name->setString("spec_bins");
		chan->value = static_cast<float>(mySpecBins);
		break;
	case 1:
		chan->name->setString("mfcc_count");
		chan->value = static_cast<float>(myMfccCount);
		break;
	default:
		break;
	}
}

// ===========================================================================
// Warning / Error
// ===========================================================================

void EssentiaSpectralCHOP::getWarningString(OP_String* warning, void* /*reserved1*/)
{
	if (!myWarning.empty())
		warning->setString(myWarning.c_str());
}

void EssentiaSpectralCHOP::getErrorString(OP_String* error, void* /*reserved1*/)
{
	if (!myError.empty())
		error->setString(myError.c_str());
}

// ===========================================================================
// Algorithm management
// ===========================================================================

void EssentiaSpectralCHOP::configureAlgorithms(int specBins,
                                                int mfccCount,
                                                int melBandCount,
                                                double sampleRate)
{
	releaseAlgorithms();

	// Resize working buffers
	mySpectrumReal.assign(static_cast<size_t>(specBins), 0.0f);
	myMfccCoeffs.assign(static_cast<size_t>(mfccCount), 0.0f);
	myMfccBands.assign(40, 0.0f);  // 40 mel bands is Essentia MFCC default
	myContrastBands.assign(kContrastBands, 0.0f);
	myContrastValleys.assign(kContrastBands, 0.0f);
	myMelBandValues.assign(static_cast<size_t>(melBandCount), 0.0f);

	// MFCC — uses 40 mel filter banks by default; numberCoefficients controls
	// how many DCT coefficients are returned (must be <= numberBands)
	myMfcc = AlgorithmFactory::create("MFCC",
		"inputSize",          specBins,
		"numberCoefficients", mfccCount,
		"numberBands",        40,
		"sampleRate",         static_cast<Real>(sampleRate));

	// Centroid — normalise=false so we get the raw bin index
	myCentroid = AlgorithmFactory::create("Centroid",
		"range", static_cast<Real>(sampleRate / 2.0));

	// Flux
	myFlux = AlgorithmFactory::create("Flux");

	// RollOff (85 % energy threshold by default)
	myRollOff = AlgorithmFactory::create("RollOff",
		"sampleRate", static_cast<Real>(sampleRate),
		"cutoff",     0.85f);

	// SpectralContrast — frameSize is needed for internal normalisation;
	// use (specBins - 1) * 2 to reconstruct the original FFT frame size.
	mySpectralContrast = AlgorithmFactory::create("SpectralContrast",
		"numberBands", kContrastBands,
		"sampleRate",  static_cast<Real>(sampleRate),
		"frameSize",   (specBins - 1) * 2);

	// HFC
	myHfc = AlgorithmFactory::create("HFC",
		"sampleRate", static_cast<Real>(sampleRate));

	// SpectralComplexity
	mySpectralComplexity = AlgorithmFactory::create("SpectralComplexity",
		"sampleRate", static_cast<Real>(sampleRate));

	// MelBands — applied directly to the spectrum
	myMelBandsAlgo = AlgorithmFactory::create("MelBands",
		"inputSize",    specBins,
		"numberBands",  melBandCount,
		"sampleRate",   static_cast<Real>(sampleRate),
		"type",         std::string("magnitude"));
}

void EssentiaSpectralCHOP::releaseAlgorithms()
{
	delete myMfcc;              myMfcc              = nullptr;
	delete myCentroid;          myCentroid          = nullptr;
	delete myFlux;              myFlux              = nullptr;
	delete myRollOff;           myRollOff           = nullptr;
	delete mySpectralContrast;  mySpectralContrast  = nullptr;
	delete myHfc;               myHfc               = nullptr;
	delete mySpectralComplexity; mySpectralComplexity = nullptr;
	delete myMelBandsAlgo;      myMelBandsAlgo      = nullptr;
}

// ===========================================================================
// Per-frame processing
// ===========================================================================

void EssentiaSpectralCHOP::processFrame(
	const std::vector<float>& spectrum,
	bool enableMfcc,    int  mfccCount,
	bool enableCentroid,
	bool enableFlux,
	bool enableRolloff,
	bool enableContrast,
	bool enableHfc,
	bool enableComplexity,
	bool enableMel,     int  melBandCount)
{
	// Copy float spectrum into essentia::Real buffer once
	const size_t bins = spectrum.size();
	mySpectrumReal.resize(bins);
	for (size_t i = 0; i < bins; ++i)
		mySpectrumReal[i] = static_cast<Real>(spectrum[i]);

	// MFCC
	if (enableMfcc && myMfcc)
	{
		try {
			myMfccCoeffs.resize(static_cast<size_t>(mfccCount), 0.0f);
			myMfcc->input("spectrum").set(mySpectrumReal);
			myMfcc->output("mfcc").set(myMfccCoeffs);
			myMfcc->output("bands").set(myMfccBands);
			myMfcc->compute();
		} catch (...) { myMfccCoeffs.assign(static_cast<size_t>(mfccCount), 0.0f); }
	}

	// Centroid
	if (enableCentroid && myCentroid)
	{
		try {
			myCentroid->input("array").set(mySpectrumReal);
			myCentroid->output("centroid").set(myCentroidVal);
			myCentroid->compute();
		} catch (...) { myCentroidVal = 0.0f; }
	}

	// Flux
	if (enableFlux && myFlux)
	{
		try {
			myFlux->input("spectrum").set(mySpectrumReal);
			myFlux->output("flux").set(myFluxVal);
			myFlux->compute();
		} catch (...) { myFluxVal = 0.0f; }
	}

	// RollOff
	if (enableRolloff && myRollOff)
	{
		try {
			myRollOff->input("spectrum").set(mySpectrumReal);
			myRollOff->output("rollOff").set(myRollOffVal);
			myRollOff->compute();
		} catch (...) { myRollOffVal = 0.0f; }
	}

	// SpectralContrast
	if (enableContrast && mySpectralContrast)
	{
		try {
			mySpectralContrast->input("spectrum").set(mySpectrumReal);
			mySpectralContrast->output("spectralContrast").set(myContrastBands);
			mySpectralContrast->output("spectralValley").set(myContrastValleys);
			mySpectralContrast->compute();
		} catch (...) {
			myContrastBands.assign(kContrastBands, 0.0f);
			myContrastValleys.assign(kContrastBands, 0.0f);
		}
	}

	// HFC
	if (enableHfc && myHfc)
	{
		try {
			myHfc->input("spectrum").set(mySpectrumReal);
			myHfc->output("hfc").set(myHfcVal);
			myHfc->compute();
		} catch (...) { myHfcVal = 0.0f; }
	}

	// SpectralComplexity
	if (enableComplexity && mySpectralComplexity)
	{
		try {
			mySpectralComplexity->input("spectrum").set(mySpectrumReal);
			mySpectralComplexity->output("spectralComplexity").set(myComplexityVal);
			mySpectralComplexity->compute();
		} catch (...) { myComplexityVal = 0.0f; }
	}

	// MelBands
	if (enableMel && myMelBandsAlgo)
	{
		try {
			myMelBandValues.resize(static_cast<size_t>(melBandCount), 0.0f);
			myMelBandsAlgo->input("spectrum").set(mySpectrumReal);
			myMelBandsAlgo->output("bands").set(myMelBandValues);
			myMelBandsAlgo->compute();
		} catch (...) { myMelBandValues.assign(static_cast<size_t>(melBandCount), 0.0f); }
	}
}

// ===========================================================================
// Channel name management
// ===========================================================================

void EssentiaSpectralCHOP::rebuildChannelNames(
	bool enableMfcc,    int mfccCount,
	bool enableCentroid,
	bool enableFlux,
	bool enableRolloff,
	bool enableContrast,
	bool enableHfc,
	bool enableComplexity,
	bool enableMel,     int melBandCount)
{
	myChannelNames.clear();

	if (enableMfcc)
	{
		for (int i = 0; i < mfccCount; ++i)
			myChannelNames.push_back("mfcc" + std::to_string(i));
	}

	if (enableCentroid)   myChannelNames.emplace_back("spectral_centroid");
	if (enableFlux)       myChannelNames.emplace_back("spectral_flux");
	if (enableRolloff)    myChannelNames.emplace_back("spectral_rolloff");

	if (enableContrast)
	{
		for (int i = 0; i < kContrastBands; ++i)
			myChannelNames.push_back("spectral_contrast" + std::to_string(i));
	}

	if (enableHfc)        myChannelNames.emplace_back("hfc");
	if (enableComplexity) myChannelNames.emplace_back("spectral_complexity");

	if (enableMel)
	{
		for (int i = 0; i < melBandCount; ++i)
			myChannelNames.push_back("mel" + std::to_string(i));
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
	ci.opType->setString("Essentiaspectral");
	ci.opLabel->setString("Essentia Spectral");
	ci.opIcon->setString("ESP");
	ci.authorName->setString("Darien Brito");
	ci.authorEmail->setString("info@darienbrito.com");
	ci.minInputs = 1;
	ci.maxInputs = 1;
}

DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
	return new EssentiaSpectralCHOP(info);
}

DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
{
	delete static_cast<EssentiaSpectralCHOP*>(instance);
}

} // extern "C"
