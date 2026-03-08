// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaSpectralCHOP.h"
#include "Shared/Utils.h"
#include "Shared/BatchFrameProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ===========================================================================
// UnifiedCHOPBase CRTP hooks
// ===========================================================================

bool EssentiaSpectralCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                              const OP_Inputs* inputs,
                                              bool isBatch)
{
	// Show / hide FFT params based on mode
	inputs->enablePar(BatchFftsizeName,     isBatch);
	inputs->enablePar(BatchHopsizeName,     isBatch);
	inputs->enablePar(BatchWindowtypeName,  isBatch);
	inputs->enablePar(BatchZeropaddingName, isBatch);

	// Read feature flags
	const bool enableMfcc       = ParametersSpectral::evalEnablemfcc(inputs);
	const int  mfccCount        = ParametersSpectral::evalMfcccount(inputs);
	const bool enableCentroid   = ParametersSpectral::evalEnablecentroid(inputs);
	const bool enableFlux       = ParametersSpectral::evalEnableflux(inputs);
	const bool enableRolloff    = ParametersSpectral::evalEnablerolloff(inputs);
	const bool enableContrast   = ParametersSpectral::evalEnablecontrast(inputs);
	const int  contrastBands    = ParametersSpectral::evalContrastbands(inputs);
	const bool enableHfc        = ParametersSpectral::evalEnablehfc(inputs);
	const bool enableComplexity = ParametersSpectral::evalEnablecomplexity(inputs);
	const bool enableMel        = ParametersSpectral::evalEnablemel(inputs);
	const int  melBandCount     = ParametersSpectral::evalMelbandscount(inputs);
	const bool melFreqNames     = ParametersSpectral::evalMelfreqnames(inputs);

	// Feature co-dependencies
	inputs->enablePar(MffcccountName,       enableMfcc);
	inputs->enablePar(MfcclowfreqName,      enableMfcc);
	inputs->enablePar(MfcchighfreqName,     enableMfcc);
	inputs->enablePar(FluxhalfrectifyName,  enableFlux);
	inputs->enablePar(FluxnormName,         enableFlux);
	inputs->enablePar(RolloffcutoffName,    enableRolloff);
	inputs->enablePar(ContrastbandsName,    enableContrast);
	inputs->enablePar(HfctypeName,          enableHfc);
	inputs->enablePar(ComplexitythreshName, enableComplexity);
	inputs->enablePar(MelbandscountName,    enableMel);
	inputs->enablePar(MellowfreqName,       enableMel);
	inputs->enablePar(MelhighfreqName,      enableMel);
	inputs->enablePar(MelfreqnamesName,     enableMel);
	inputs->enablePar(MellogName,           enableMel);

	// Count output channels
	int numCh = 0;
	if (enableMfcc)       numCh += mfccCount;
	if (enableCentroid)   numCh += 1;
	if (enableFlux)       numCh += 1;
	if (enableRolloff)    numCh += 1;
	if (enableContrast)   numCh += contrastBands;
	if (enableHfc)        numCh += 1;
	if (enableComplexity) numCh += 1;
	if (enableMel)        numCh += melBandCount;

	info->numChannels = (numCh > 0) ? numCh : 1;

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

	// Rebuild channel names from current param state (RT path; batch overwrites on result)
	const double sampleRate = isBatch ? 44100.0 : inputs->getTimeInfo()->rate;
	rebuildChannelNames(enableMfcc, mfccCount,
	                    enableCentroid, enableFlux, enableRolloff,
	                    enableContrast, enableHfc, enableComplexity,
	                    enableMel, melBandCount,
	                    melFreqNames, sampleRate);

	return true;
}

void EssentiaSpectralCHOP::getChannelNameImpl(int32_t index,
                                               OP_String* name,
                                               const OP_Inputs*)
{
	if (index >= 0 && index < static_cast<int32_t>(myChannelNames.size()))
		name->setString(myChannelNames[static_cast<size_t>(index)].c_str());
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------
// Real-time execution
// ---------------------------------------------------------------------------

void EssentiaSpectralCHOP::executeRealtimeImpl(CHOP_Output* output,
                                                const OP_Inputs* inputs)
{
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		zeroOutput(output);
		return;
	}

	// Read parameters
	const bool  enableMfcc       = ParametersSpectral::evalEnablemfcc(inputs);
	const int   mfccCount        = ParametersSpectral::evalMfcccount(inputs);
	const float mfccLowFreq      = ParametersSpectral::evalMfcclowfreq(inputs);
	const float mfccHighFreq     = ParametersSpectral::evalMfcchighfreq(inputs);
	const bool  enableCentroid   = ParametersSpectral::evalEnablecentroid(inputs);
	const bool  enableFlux       = ParametersSpectral::evalEnableflux(inputs);
	const bool  fluxHalfRect     = ParametersSpectral::evalFluxhalfrectify(inputs);
	const int   fluxNorm         = ParametersSpectral::evalFluxnorm(inputs);
	const bool  enableRolloff    = ParametersSpectral::evalEnablerolloff(inputs);
	const float rolloffCutoff    = ParametersSpectral::evalRolloffcutoff(inputs);
	const bool  enableContrast   = ParametersSpectral::evalEnablecontrast(inputs);
	const int   contrastBands    = ParametersSpectral::evalContrastbands(inputs);
	const bool  enableHfc        = ParametersSpectral::evalEnablehfc(inputs);
	const int   hfcType          = ParametersSpectral::evalHfctype(inputs);
	const bool  enableComplexity = ParametersSpectral::evalEnablecomplexity(inputs);
	const float complexityThresh = ParametersSpectral::evalComplexitythresh(inputs);
	const bool  enableMel        = ParametersSpectral::evalEnablemel(inputs);
	const int   melBandCount     = ParametersSpectral::evalMelbandscount(inputs);
	const float melLowFreq       = ParametersSpectral::evalMellowfreq(inputs);
	const float melHighFreq      = ParametersSpectral::evalMelhighfreq(inputs);
	const bool  melFreqNames     = ParametersSpectral::evalMelfreqnames(inputs);

	// Validate input
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1)
	{
		myError = "No input connected — connect EssentiaCoreCHOP";
		zeroOutput(output);
		return;
	}

	// Extract spectrum from input
	std::vector<float> spectrumF;
	if (!extractChannelSamples(chopIn, "spectrum", spectrumF) || spectrumF.empty())
	{
		myError = "Input has no spectrum channel — connect EssentiaSpectrumCHOP";
		zeroOutput(output);
		return;
	}

	const int    specBins   = static_cast<int>(spectrumF.size());
	const double sampleRate = (chopIn->sampleRate > 0.0) ? chopIn->sampleRate : 44100.0;

	// Build desired algorithm config
	AlgoConfig newCfg;
	newCfg.specBins         = specBins;
	newCfg.mfccCount        = mfccCount;
	newCfg.melBandCount     = melBandCount;
	newCfg.contrastBands    = contrastBands;
	newCfg.sampleRate       = sampleRate;
	newCfg.mfccLowFreq      = mfccLowFreq;
	newCfg.mfccHighFreq     = mfccHighFreq;
	newCfg.rolloffCutoff    = rolloffCutoff;
	newCfg.hfcType          = hfcType;
	newCfg.fluxHalfRect     = fluxHalfRect;
	newCfg.fluxNorm         = fluxNorm;
	newCfg.complexityThresh = complexityThresh;
	newCfg.melLowFreq       = melLowFreq;
	newCfg.melHighFreq      = melHighFreq;

	// Detect configuration change
	const bool featureFlagsChanged =
		(enableMfcc       != myPrevEnableMfcc)      ||
		(enableCentroid   != myPrevEnableCentroid)   ||
		(enableFlux       != myPrevEnableFlux)       ||
		(enableRolloff    != myPrevEnableRolloff)    ||
		(enableContrast   != myPrevEnableContrast)   ||
		(enableHfc        != myPrevEnableHfc)        ||
		(enableComplexity != myPrevEnableComplexity) ||
		(mfccCount        != myPrevMfccCount)        ||
		(enableMel        != myPrevEnableMel)        ||
		(melBandCount     != myPrevMelBandCount)     ||
		(melFreqNames     != myPrevMelFreqNames)     ||
		(contrastBands    != myPrevContrastBands);

	const bool configChanged =
		(specBins         != myCfg.specBins)         ||
		(mfccCount        != myCfg.mfccCount)        ||
		(melBandCount     != myCfg.melBandCount)     ||
		(sampleRate       != myCfg.sampleRate)       ||
		(mfccLowFreq      != myCfg.mfccLowFreq)      ||
		(mfccHighFreq     != myCfg.mfccHighFreq)     ||
		(rolloffCutoff    != myCfg.rolloffCutoff)    ||
		(hfcType          != myCfg.hfcType)          ||
		(fluxHalfRect     != myCfg.fluxHalfRect)     ||
		(fluxNorm         != myCfg.fluxNorm)         ||
		(complexityThresh != myCfg.complexityThresh) ||
		(contrastBands    != myCfg.contrastBands)    ||
		(melLowFreq       != myCfg.melLowFreq)       ||
		(melHighFreq      != myCfg.melHighFreq)      ||
		featureFlagsChanged;

	if (configChanged)
	{
		try
		{
			configureAlgorithms(newCfg);
			myCfg = newCfg;

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
			myPrevMelFreqNames     = melFreqNames;
			myPrevContrastBands    = contrastBands;
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
		                    enableMel, melBandCount,
		                    melFreqNames, sampleRate);
	}

	// Run algorithms
	processFrame(spectrumF,
	             enableMfcc,   mfccCount,
	             enableCentroid,
	             enableFlux,
	             enableRolloff,
	             enableContrast,
	             enableHfc,
	             enableComplexity,
	             enableMel,    melBandCount);

	// Write output channels
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
		for (int i = 0; i < myContrastBands; ++i)
		{
			const float val = (i < static_cast<int>(myContrastValues.size()))
			                  ? static_cast<float>(myContrastValues[static_cast<size_t>(i)])
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
		const bool melLog = ParametersSpectral::evalMellog(inputs);

		for (int i = 0; i < melBandCount; ++i)
		{
			float val = (i < static_cast<int>(myMelBandValues.size()))
			            ? static_cast<float>(myMelBandValues[static_cast<size_t>(i)])
			            : 0.0f;
			if (melLog)
				val = 20.0f * std::log10(std::max(val, 1e-10f));
			if (ch < output->numChannels)
				output->channels[ch][0] = val;
			++ch;
		}
	}
}

// ---------------------------------------------------------------------------
// Batch: snapshot parameters and launch async worker
// ---------------------------------------------------------------------------

void EssentiaSpectralCHOP::snapshotAndLaunch(AudioSnapshot audio,
                                              const OP_Inputs* inputs)
{
	BatchSpectralParams params;
	params.fftSize       = evalBatchFftsize(inputs);
	params.hopSize       = evalBatchHopsize(inputs);
	params.windowType    = evalBatchWindowtype(inputs);
	params.zeroPadFactor = evalBatchZeropadding(inputs);

	params.enableMfcc   = ParametersSpectral::evalEnablemfcc(inputs);
	params.mfccCount    = ParametersSpectral::evalMfcccount(inputs);
	params.mfccLowFreq  = ParametersSpectral::evalMfcclowfreq(inputs);
	params.mfccHighFreq = ParametersSpectral::evalMfcchighfreq(inputs);

	params.enableCentroid = ParametersSpectral::evalEnablecentroid(inputs);
	params.enableFlux     = ParametersSpectral::evalEnableflux(inputs);
	params.fluxHalfRect   = ParametersSpectral::evalFluxhalfrectify(inputs);
	params.fluxNorm       = ParametersSpectral::evalFluxnorm(inputs);
	params.enableRolloff  = ParametersSpectral::evalEnablerolloff(inputs);
	params.rolloffCutoff  = ParametersSpectral::evalRolloffcutoff(inputs);

	params.enableContrast = ParametersSpectral::evalEnablecontrast(inputs);
	params.contrastBands  = ParametersSpectral::evalContrastbands(inputs);

	params.enableHfc = ParametersSpectral::evalEnablehfc(inputs);
	params.hfcType   = ParametersSpectral::evalHfctype(inputs);

	params.enableComplexity = ParametersSpectral::evalEnablecomplexity(inputs);
	params.complexThresh    = ParametersSpectral::evalComplexitythresh(inputs);

	params.enableMel    = ParametersSpectral::evalEnablemel(inputs);
	params.melBandCount = ParametersSpectral::evalMelbandscount(inputs);
	params.melLowFreq   = ParametersSpectral::evalMellowfreq(inputs);
	params.melHighFreq  = ParametersSpectral::evalMelhighfreq(inputs);
	params.melLog       = ParametersSpectral::evalMellog(inputs);

	myRunner.launch(
		[audio  = std::move(audio),
		 params = std::move(params)]
		(const std::atomic<bool>& cancelFlag,
		 std::atomic<float>&      progress) -> AsyncBatchResult
		{
			return computeBatchAsync(audio, params, cancelFlag, progress);
		});
}

// ---------------------------------------------------------------------------
// Batch: extract channel names from completed result
// ---------------------------------------------------------------------------

void EssentiaSpectralCHOP::onResultCollected(AsyncBatchResult& result)
{
	myChannelNames = std::move(result.channelNames);
}

// ---------------------------------------------------------------------------
// Parameter setup
// ---------------------------------------------------------------------------

void EssentiaSpectralCHOP::setupParametersImpl(OP_ParameterManager* manager)
{
	ParametersSpectral::setup(manager);
}

// ===========================================================================
// Info CHOP — always 6 channels; batch-specific ones show 0 in RT mode
// ===========================================================================

int32_t EssentiaSpectralCHOP::getNumInfoCHOPChansImpl()
{
	return 6;
}

void EssentiaSpectralCHOP::getInfoCHOPChanImpl(int32_t index,
                                                OP_InfoCHOPChan* chan)
{
	switch (index)
	{
	case 0:
		chan->name->setString("spec_bins");
		chan->value = static_cast<float>(myCfg.specBins);
		break;
	case 1:
		chan->name->setString("mfcc_count");
		chan->value = static_cast<float>(myCfg.mfccCount);
		break;
	case 2:
		chan->name->setString("num_frames");
		chan->value = static_cast<float>(myCachedNumFrames);
		break;
	case 3:
		chan->name->setString("num_channels");
		chan->value = static_cast<float>(myResultCache.size());
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

// ===========================================================================
// Algorithm management
// ===========================================================================

void EssentiaSpectralCHOP::configureAlgorithms(const AlgoConfig& cfg)
{
	releaseAlgorithms();

	const Real sr = static_cast<Real>(cfg.sampleRate);
	myContrastBands = cfg.contrastBands;

	// Resize working buffers
	mySpectrumReal.assign(static_cast<size_t>(cfg.specBins), 0.0f);
	myMfccCoeffs.assign(static_cast<size_t>(cfg.mfccCount), 0.0f);
	myMfccBands.assign(40, 0.0f);
	myContrastValues.assign(static_cast<size_t>(cfg.contrastBands), 0.0f);
	myContrastValleys.assign(static_cast<size_t>(cfg.contrastBands), 0.0f);
	myMelBandValues.assign(static_cast<size_t>(cfg.melBandCount), 0.0f);

	// MFCC
	myMfcc = AlgorithmFactory::create("MFCC",
		"inputSize",          cfg.specBins,
		"numberCoefficients", cfg.mfccCount,
		"numberBands",        40,
		"sampleRate",         sr,
		"lowFrequencyBound",  static_cast<Real>(cfg.mfccLowFreq),
		"highFrequencyBound", static_cast<Real>(cfg.mfccHighFreq));

	// Centroid
	myCentroid = AlgorithmFactory::create("Centroid",
		"range", static_cast<Real>(cfg.sampleRate / 2.0));

	// Flux
	static const char* normNames[] = { "L1", "L2" };
	myFlux = AlgorithmFactory::create("Flux",
		"halfRectify", cfg.fluxHalfRect,
		"norm",        std::string(normNames[std::clamp(cfg.fluxNorm, 0, 1)]));

	// RollOff
	myRollOff = AlgorithmFactory::create("RollOff",
		"sampleRate", sr,
		"cutoff",     static_cast<Real>(cfg.rolloffCutoff));

	// SpectralContrast
	mySpectralContrast = AlgorithmFactory::create("SpectralContrast",
		"numberBands", cfg.contrastBands,
		"sampleRate",  sr,
		"frameSize",   (cfg.specBins - 1) * 2);

	// HFC
	static const char* hfcNames[] = { "Masri", "Jensen", "Brossier" };
	myHfc = AlgorithmFactory::create("HFC",
		"sampleRate", sr,
		"type",       std::string(hfcNames[std::clamp(cfg.hfcType, 0, 2)]));

	// SpectralComplexity
	mySpectralComplexity = AlgorithmFactory::create("SpectralComplexity",
		"sampleRate",         sr,
		"magnitudeThreshold", static_cast<Real>(cfg.complexityThresh));

	// MelBands
	myMelBandsAlgo = AlgorithmFactory::create("MelBands",
		"inputSize",          cfg.specBins,
		"numberBands",        cfg.melBandCount,
		"sampleRate",         sr,
		"type",               std::string("magnitude"),
		"lowFrequencyBound",  static_cast<Real>(cfg.melLowFreq),
		"highFrequencyBound", static_cast<Real>(cfg.melHighFreq));
}

void EssentiaSpectralCHOP::releaseAlgorithms()
{
	delete myMfcc;               myMfcc               = nullptr;
	delete myCentroid;           myCentroid           = nullptr;
	delete myFlux;               myFlux               = nullptr;
	delete myRollOff;            myRollOff            = nullptr;
	delete mySpectralContrast;   mySpectralContrast   = nullptr;
	delete myHfc;                myHfc                = nullptr;
	delete mySpectralComplexity; mySpectralComplexity = nullptr;
	delete myMelBandsAlgo;       myMelBandsAlgo       = nullptr;
}

// ===========================================================================
// Per-frame processing (RT path)
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
			mySpectralContrast->output("spectralContrast").set(myContrastValues);
			mySpectralContrast->output("spectralValley").set(myContrastValleys);
			mySpectralContrast->compute();
		} catch (...) {
			myContrastValues.assign(static_cast<size_t>(myContrastBands), 0.0f);
			myContrastValleys.assign(static_cast<size_t>(myContrastBands), 0.0f);
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
	bool enableMfcc,    int  mfccCount,
	bool enableCentroid,
	bool enableFlux,
	bool enableRolloff,
	bool enableContrast,
	bool enableHfc,
	bool enableComplexity,
	bool enableMel,     int  melBandCount,
	bool melFreqNames,  double sampleRate)
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
		for (int i = 0; i < myContrastBands; ++i)
			myChannelNames.push_back("spectral_contrast" + std::to_string(i));
	}

	if (enableHfc)        myChannelNames.emplace_back("hfc");
	if (enableComplexity) myChannelNames.emplace_back("spectral_complexity");

	if (enableMel)
	{
		if (melFreqNames)
		{
			const double lowFreq  = static_cast<double>(myCfg.melLowFreq);
			const double highFreq = static_cast<double>(myCfg.melHighFreq);
			const double melLow   = 1127.01048 * std::log(1.0 + lowFreq  / 700.0);
			const double melHigh  = 1127.01048 * std::log(1.0 + highFreq / 700.0);

			std::vector<int> edges(melBandCount + 2);
			for (int i = 0; i < melBandCount + 2; ++i)
			{
				double mel = melLow + (melHigh - melLow) * i / (melBandCount + 1);
				edges[i]   = static_cast<int>(700.0 * (std::exp(mel / 1127.01048) - 1.0));
			}

			for (int i = 0; i < melBandCount; ++i)
			{
				myChannelNames.push_back("mel" + std::to_string(i)
					+ "_" + std::to_string(edges[i])
					+ "_" + std::to_string(edges[i + 2]));
			}
		}
		else
		{
			for (int i = 0; i < melBandCount; ++i)
				myChannelNames.push_back("mel" + std::to_string(i));
		}
	}
}

// ===========================================================================
// Static async worker — NO access to `this`
// ===========================================================================

AsyncBatchResult EssentiaSpectralCHOP::computeBatchAsync(
	const AudioSnapshot&       audio,
	const BatchSpectralParams& params,
	const std::atomic<bool>&   cancelFlag,
	std::atomic<float>&        progress)
{
	AsyncBatchResult result;
	result.success = false;

	// Derive zero-padding amount from factor
	const int zeroPad = zeroPadFromFactor(params.zeroPadFactor, params.fftSize);

	const double sampleRate = audio.sampleRate;

	// Frame processing (local instance, owns its Essentia algorithms)
	BatchFrameProcessor frameProc;
	frameProc.configure(params.fftSize, params.hopSize, params.windowType, zeroPad);
	frameProc.processAllFrames(audio.data.data(), audio.numSamples);

	const int numFrames = frameProc.numFrames();
	const int specBins  = frameProc.specBins();

	if (numFrames == 0)
	{
		result.warning = "Audio too short for given FFT size";
		result.success  = false;
		return result;
	}

	// Build channel name list
	{
		std::vector<std::string>& names = result.channelNames;

		if (params.enableMfcc)
			for (int i = 0; i < params.mfccCount; ++i)
				names.push_back("mfcc" + std::to_string(i));

		if (params.enableCentroid)   names.emplace_back("spectral_centroid");
		if (params.enableFlux)       names.emplace_back("spectral_flux");
		if (params.enableRolloff)    names.emplace_back("spectral_rolloff");

		if (params.enableContrast)
			for (int i = 0; i < params.contrastBands; ++i)
				names.push_back("spectral_contrast" + std::to_string(i));

		if (params.enableHfc)        names.emplace_back("hfc");
		if (params.enableComplexity) names.emplace_back("spectral_complexity");

		if (params.enableMel)
			for (int i = 0; i < params.melBandCount; ++i)
				names.push_back("mel" + std::to_string(i));
	}

	const int numCh = static_cast<int>(result.channelNames.size());
	result.cache.assign(numCh, std::vector<float>(numFrames, 0.0f));

	// Create Essentia algorithms locally
	const Real sr = static_cast<Real>(sampleRate);

	Algorithm* algoMfcc             = nullptr;
	Algorithm* algoCentroid         = nullptr;
	Algorithm* algoFlux             = nullptr;
	Algorithm* algoRollOff          = nullptr;
	Algorithm* algoSpectralContrast = nullptr;
	Algorithm* algoHfc              = nullptr;
	Algorithm* algoComplexity       = nullptr;
	Algorithm* algoMelBands         = nullptr;

	if (params.enableMfcc)
		algoMfcc = AlgorithmFactory::create("MFCC",
			"inputSize",          specBins,
			"numberCoefficients", params.mfccCount,
			"numberBands",        40,
			"sampleRate",         sr,
			"lowFrequencyBound",  static_cast<Real>(params.mfccLowFreq),
			"highFrequencyBound", static_cast<Real>(params.mfccHighFreq));

	if (params.enableCentroid)
		algoCentroid = AlgorithmFactory::create("Centroid",
			"range", static_cast<Real>(sampleRate / 2.0));

	if (params.enableFlux)
		algoFlux = AlgorithmFactory::create("Flux",
			"halfRectify", params.fluxHalfRect,
			"norm",        (params.fluxNorm == 0) ? "L1" : "L2");

	if (params.enableRolloff)
		algoRollOff = AlgorithmFactory::create("RollOff",
			"sampleRate", sr,
			"cutoff",     static_cast<Real>(params.rolloffCutoff));

	if (params.enableContrast)
		algoSpectralContrast = AlgorithmFactory::create("SpectralContrast",
			"sampleRate",  sr,
			"frameSize",   (specBins - 1) * 2,
			"numberBands", params.contrastBands);

	if (params.enableHfc)
		algoHfc = AlgorithmFactory::create("HFC",
			"sampleRate", sr);

	if (params.enableComplexity)
		algoComplexity = AlgorithmFactory::create("SpectralComplexity",
			"sampleRate",         sr,
			"magnitudeThreshold", static_cast<Real>(params.complexThresh));

	if (params.enableMel)
		algoMelBands = AlgorithmFactory::create("MelBands",
			"inputSize",          specBins,
			"numberBands",        params.melBandCount,
			"sampleRate",         sr,
			"lowFrequencyBound",  static_cast<Real>(params.melLowFreq),
			"highFrequencyBound", static_cast<Real>(params.melHighFreq));

	// Pre-allocate output buffers
	std::vector<Real> mfccCoeffs(params.mfccCount, 0.0f);
	std::vector<Real> mfccBands(40, 0.0f);
	Real              centroidVal   = 0.0f;
	Real              fluxVal       = 0.0f;
	Real              rollOffVal    = 0.0f;
	std::vector<Real> contrastVals(params.contrastBands, 0.0f);
	std::vector<Real> contrastValleys(params.contrastBands, 0.0f);
	Real              hfcVal        = 0.0f;
	Real              complexityVal = 0.0f;
	std::vector<Real> melBandVals(params.melBandCount, 0.0f);

	// Per-frame processing loop
	for (int f = 0; f < numFrames; ++f)
	{
		// Cancel check every 64 frames
		if ((f & 63) == 0)
		{
			if (cancelFlag.load(std::memory_order_relaxed))
				goto cleanup;

			progress.store(static_cast<float>(f) / static_cast<float>(numFrames),
			               std::memory_order_relaxed);
		}

		{
			const auto& spectrum = frameProc.getSpectrum(f);
			int ch = 0;

			// MFCC
			if (algoMfcc)
			{
				try {
					algoMfcc->input("spectrum").set(spectrum);
					algoMfcc->output("mfcc").set(mfccCoeffs);
					algoMfcc->output("bands").set(mfccBands);
					algoMfcc->compute();
				} catch (...) { mfccCoeffs.assign(params.mfccCount, 0.0f); }

				for (int i = 0; i < params.mfccCount && ch < numCh; ++i)
					result.cache[ch++][f] = (i < static_cast<int>(mfccCoeffs.size()))
					                        ? static_cast<float>(mfccCoeffs[i]) : 0.0f;
			}

			// Centroid
			if (algoCentroid)
			{
				try {
					algoCentroid->input("array").set(spectrum);
					algoCentroid->output("centroid").set(centroidVal);
					algoCentroid->compute();
				} catch (...) { centroidVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(centroidVal);
			}

			// Flux
			if (algoFlux)
			{
				try {
					algoFlux->input("spectrum").set(spectrum);
					algoFlux->output("flux").set(fluxVal);
					algoFlux->compute();
				} catch (...) { fluxVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(fluxVal);
			}

			// RollOff
			if (algoRollOff)
			{
				try {
					algoRollOff->input("spectrum").set(spectrum);
					algoRollOff->output("rollOff").set(rollOffVal);
					algoRollOff->compute();
				} catch (...) { rollOffVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(rollOffVal);
			}

			// SpectralContrast
			if (algoSpectralContrast)
			{
				try {
					algoSpectralContrast->input("spectrum").set(spectrum);
					algoSpectralContrast->output("spectralContrast").set(contrastVals);
					algoSpectralContrast->output("spectralValley").set(contrastValleys);
					algoSpectralContrast->compute();
				} catch (...) { contrastVals.assign(params.contrastBands, 0.0f); }

				for (int i = 0; i < params.contrastBands && ch < numCh; ++i)
					result.cache[ch++][f] = (i < static_cast<int>(contrastVals.size()))
					                        ? static_cast<float>(contrastVals[i]) : 0.0f;
			}

			// HFC
			if (algoHfc)
			{
				try {
					algoHfc->input("spectrum").set(spectrum);
					algoHfc->output("hfc").set(hfcVal);
					algoHfc->compute();
				} catch (...) { hfcVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(hfcVal);
			}

			// SpectralComplexity
			if (algoComplexity)
			{
				try {
					algoComplexity->input("spectrum").set(spectrum);
					algoComplexity->output("spectralComplexity").set(complexityVal);
					algoComplexity->compute();
				} catch (...) { complexityVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(complexityVal);
			}

			// MelBands
			if (algoMelBands)
			{
				try {
					algoMelBands->input("spectrum").set(spectrum);
					algoMelBands->output("bands").set(melBandVals);
					algoMelBands->compute();
				} catch (...) { melBandVals.assign(params.melBandCount, 0.0f); }

				for (int i = 0; i < params.melBandCount && ch < numCh; ++i)
				{
					float val = (i < static_cast<int>(melBandVals.size()))
					            ? static_cast<float>(melBandVals[i]) : 0.0f;
					if (params.melLog)
						val = 20.0f * std::log10(std::max(val, 1e-10f));
					result.cache[ch++][f] = val;
				}
			}
		}
	}

	progress.store(1.0f, std::memory_order_relaxed);
	result.numFrames  = numFrames;
	result.sampleRate = static_cast<float>(sampleRate / params.hopSize);
	result.success    = true;

cleanup:
	delete algoMfcc;
	delete algoCentroid;
	delete algoFlux;
	delete algoRollOff;
	delete algoSpectralContrast;
	delete algoHfc;
	delete algoComplexity;
	delete algoMelBands;

	return result;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

UNIFIED_CHOP_DLL_EXPORT(EssentiaSpectralCHOP, "Essentiaspectral", "Essentia Spectral", "ESP")
