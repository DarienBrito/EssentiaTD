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
	// FFT size/window drive BOTH modes (v2.0: RT runs its own FFT).
	// Hop is batch-only (RT analyzes the latest window once per cook);
	// zero-padding is batch-only (it narrows bin spacing without improving
	// true resolution).
	inputs->enablePar(BatchFftsizeName,     true);
	inputs->enablePar(BatchHopsizeName,     isBatch);
	inputs->enablePar(BatchWindowtypeName,  true);
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

	// PCA params
	const bool enablePca     = ParametersSpectral::evalEnablepca(inputs);
	const int  pcaComponents = ParametersSpectral::evalPcacomponents(inputs);
	const bool pcaVariance   = ParametersSpectral::evalPcavariance(inputs);

	inputs->enablePar(PcacomponentsName, enablePca);
	inputs->enablePar(PcawindowsizeName, enablePca && !isBatch);
	inputs->enablePar(PcaupdaterateName, enablePca && !isBatch);
	inputs->enablePar(PcavarianceName,   enablePca);

	// Batch with cached results: channel names/count must match the
	// compute-time snapshot (onResultCollected) — cache rows are copied
	// positionally, so rebuilding from live params after a feature toggle
	// would silently mislabel/misalign the cached data
	if (isBatch && myHasResults)
	{
		info->numChannels = std::max(1, static_cast<int>(myChannelNames.size()));
		info->numSamples  = myCachedNumFrames;
		info->startIndex  = 0;
		info->sampleRate  = myCachedSampleRate;
		return true;
	}

	// Count spectral output channels
	int numSpectralCh = 0;
	if (enableMfcc)       numSpectralCh += mfccCount;
	if (enableCentroid)   numSpectralCh += 1;
	if (enableFlux)       numSpectralCh += 1;
	if (enableRolloff)    numSpectralCh += 1;
	if (enableContrast)   numSpectralCh += contrastBands;
	if (enableHfc)        numSpectralCh += 1;
	if (enableComplexity) numSpectralCh += 1;
	if (enableMel)        numSpectralCh += melBandCount;

	// PCA channels
	int numCh = numSpectralCh;
	int effectivePcaComponents = 0;
	if (enablePca && numSpectralCh > 0)
	{
		effectivePcaComponents = std::min(pcaComponents, numSpectralCh);
		numCh += effectivePcaComponents;
		if (pcaVariance) numCh += effectivePcaComponents;
	}

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
		info->sampleRate = static_cast<float>(sanitizedCookRate(inputs));
	}

	// Rebuild channel names from current param state (RT path; batch overwrites on result)
	const double sampleRate = isBatch ? 44100.0 : sanitizedCookRate(inputs);
	rebuildChannelNames(enableMfcc, mfccCount,
	                    enableCentroid, enableFlux, enableRolloff,
	                    enableContrast, enableHfc, enableComplexity,
	                    enableMel, melBandCount,
	                    melFreqNames, sampleRate);

	// Append PCA channel names
	if (enablePca && effectivePcaComponents > 0)
	{
		for (int i = 0; i < effectivePcaComponents; ++i)
			myChannelNames.push_back("pc" + std::to_string(i));
		if (pcaVariance)
			for (int i = 0; i < effectivePcaComponents; ++i)
				myChannelNames.push_back("pc_var" + std::to_string(i));
	}

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

	// PCA params
	const bool  enablePca        = ParametersSpectral::evalEnablepca(inputs);
	const int   pcaComponents    = ParametersSpectral::evalPcacomponents(inputs);
	const int   pcaWindowSize    = ParametersSpectral::evalPcawindowsize(inputs);
	const int   pcaUpdateRate    = ParametersSpectral::evalPcaupdaterate(inputs);
	const bool  pcaVariance      = ParametersSpectral::evalPcavariance(inputs);

	// Get audio input — v2.0: this op analyzes raw audio and runs its own FFT
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1 || chopIn->numSamples < 1)
	{
		myError = "No audio input connected";
		zeroOutput(output);
		return;
	}
	if (const char* contract = spectrumInputContractError(chopIn))
	{
		myError = contract;
		zeroOutput(output);
		return;
	}

	const double sampleRate = (chopIn->sampleRate > 0.0) ? chopIn->sampleRate : 44100.0;

	if (chopIn->numChannels > 1)
		addWarning(WarnMultichannel, "Analyzing channel 0 only");

	const int         fftSize = evalBatchFftsize(inputs);
	const std::string winType = evalBatchWindowtype(inputs);

	if (fftSize != myRtFftSize || winType != myRtWindowType)
	{
		try
		{
			myFrameProc.configure(fftSize, winType, 0);  // RT: no zero-padding
			myRtFftSize    = fftSize;
			myRtWindowType = winType;
		}
		catch (const std::exception& e)
		{
			myError = std::string("FFT config failed: ") + e.what();
			myFrameProc.release();
			zeroOutput(output);
			return;
		}
		catch (...)
		{
			myError = "FFT config failed with unknown error";
			myFrameProc.release();
			zeroOutput(output);
			return;
		}
	}

	myFrameProc.accumulate(chopIn->getChannelData(0),
	                       static_cast<size_t>(chopIn->numSamples),
	                       chopIn->totalCooks);

	// Empty unless the timeslice exceeds the window (audio being skipped)
	addWarning(WarnResolution, myFrameProc.coverageWarning());

	if (!myFrameProc.processLatest())
	{
		// Warming up (~fftSize/sampleRate seconds); hold zeros
		addWarning(WarnTransient, myFrameProc.accumulatingWarning());
		zeroOutput(output);
		return;
	}

	const int specBins = myFrameProc.specBins();

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
	}

	const bool pcaFlagsChanged =
		(enablePca      != myPrevEnablePca)      ||
		(pcaComponents  != myPrevPcaComponents)  ||
		(pcaVariance    != myPrevPcaVariance);

	if (configChanged || pcaFlagsChanged)
	{
		rebuildChannelNames(enableMfcc, mfccCount,
		                    enableCentroid, enableFlux, enableRolloff,
		                    enableContrast, enableHfc, enableComplexity,
		                    enableMel, melBandCount,
		                    melFreqNames, sampleRate);

		// Append PCA channel names
		const int numSpectralCh = static_cast<int>(myChannelNames.size());
		if (enablePca && numSpectralCh > 0)
		{
			const int nc = std::min(pcaComponents, numSpectralCh);
			for (int i = 0; i < nc; ++i)
				myChannelNames.push_back("pc" + std::to_string(i));
			if (pcaVariance)
				for (int i = 0; i < nc; ++i)
					myChannelNames.push_back("pc_var" + std::to_string(i));
		}

		if (pcaFlagsChanged)
		{
			myPrevEnablePca      = enablePca;
			myPrevPcaComponents  = pcaComponents;
			myPrevPcaVariance    = pcaVariance;
		}
	}

	// Surface any per-feature construction failure on every cook (warning
	// slots are cleared at the top of execute)
	addWarning(WarnAlgo, myConfigWarning);

	// Run algorithms on the internally computed magnitude spectrum
	processFrame(myFrameProc.magnitude(),
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
				val = 10.0f * std::log10(std::max(val, 1e-10f));
			if (ch < output->numChannels)
				output->channels[ch][0] = val;
			++ch;
		}
	}

	// PCA
	if (enablePca)
	{
		const int numSpectralCh = ch;
		if (numSpectralCh > 0)
		{
			const int nc = std::min(pcaComponents, numSpectralCh);
			myPca.configure(numSpectralCh, nc, pcaWindowSize);

			// Collect spectral features from output channels
			myPcaFeatureBuffer.resize(static_cast<size_t>(numSpectralCh));
			for (int i = 0; i < numSpectralCh; ++i)
				myPcaFeatureBuffer[static_cast<size_t>(i)] = output->channels[i][0];

			myPca.pushFrame(myPcaFeatureBuffer.data(), numSpectralCh);

			// Throttled recompute — floor of 5 cooks: eigendecomp is
			// O(N·D²+D³) on the cook thread, and Pcaupdaterate at or above
			// the project frame rate would otherwise run it every cook
			const float frameRate = static_cast<float>(sanitizedCookRate(inputs));
			const int framesPerUpdate = std::max(5,
				static_cast<int>(frameRate / static_cast<float>(pcaUpdateRate)));
			++myPcaUpdateCounter;
			if (myPcaUpdateCounter >= framesPerUpdate)
			{
				myPca.recompute();
				myPcaUpdateCounter = 0;
			}

			// Project
			myPcaProjectBuffer.resize(static_cast<size_t>(nc));
			myPca.project(myPcaFeatureBuffer.data(), myPcaProjectBuffer.data());

			for (int c = 0; c < nc && ch < output->numChannels; ++c)
				output->channels[ch++][0] = myPcaProjectBuffer[static_cast<size_t>(c)];

			// Variance channels
			if (pcaVariance)
			{
				for (int c = 0; c < nc && ch < output->numChannels; ++c)
					output->channels[ch++][0] = myPca.varianceRatio(c);
			}
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
	params.melFreqNames = ParametersSpectral::evalMelfreqnames(inputs);

	params.enablePca     = ParametersSpectral::evalEnablepca(inputs);
	params.pcaComponents = ParametersSpectral::evalPcacomponents(inputs);
	params.pcaVariance   = ParametersSpectral::evalPcavariance(inputs);

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

// Clamp a low/high frequency pair to a usable filterbank range. The pairs
// are user-editable independently; an inverted, super-Nyquist, or
// near-degenerate span makes MFCC/MelBands construction throw ("spectrum
// bins insufficient" once the mel bands outnumber the FFT bins in range).
// lo <= hi/2 guarantees the band span always covers enough bins.
static void clampFreqBounds(float& lo, float& hi, double sampleRate)
{
	const float nyquist = static_cast<float>(sampleRate * 0.5);
	hi = std::clamp(hi, 100.0f, nyquist);
	lo = std::clamp(lo, 0.0f, hi * 0.5f);
}

void EssentiaSpectralCHOP::configureAlgorithms(const AlgoConfig& cfg)
{
	releaseAlgorithms();

	const Real sr = static_cast<Real>(cfg.sampleRate);
	myContrastBands = cfg.contrastBands;

	float mfccLo = cfg.mfccLowFreq, mfccHi = cfg.mfccHighFreq;
	clampFreqBounds(mfccLo, mfccHi, cfg.sampleRate);
	float melLo = cfg.melLowFreq, melHi = cfg.melHighFreq;
	clampFreqBounds(melLo, melHi, cfg.sampleRate);

	// Resize working buffers
	mySpectrumReal.assign(static_cast<size_t>(cfg.specBins), 0.0f);
	myMfccCoeffs.assign(static_cast<size_t>(cfg.mfccCount), 0.0f);
	myMfccBands.assign(40, 0.0f);
	myContrastValues.assign(static_cast<size_t>(cfg.contrastBands), 0.0f);
	myContrastValleys.assign(static_cast<size_t>(cfg.contrastBands), 0.0f);
	myMelBandValues.assign(static_cast<size_t>(cfg.melBandCount), 0.0f);

	// Each create is guarded individually — a pathological config must only
	// disable that one feature (its pointer stays null and processFrame
	// skips it), not abort construction of everything after it
	std::string failed;
	auto note = [&failed](const char* n)
	{
		if (!failed.empty()) failed += ", ";
		failed += n;
	};

	// MFCC
	try {
		myMfcc = AlgorithmFactory::create("MFCC",
			"inputSize",          cfg.specBins,
			"numberCoefficients", cfg.mfccCount,
			"numberBands",        40,
			"sampleRate",         sr,
			"lowFrequencyBound",  static_cast<Real>(mfccLo),
			"highFrequencyBound", static_cast<Real>(mfccHi));
	} catch (...) { note("MFCC"); }

	// Centroid
	try {
		myCentroid = AlgorithmFactory::create("Centroid",
			"range", static_cast<Real>(cfg.sampleRate / 2.0));
	} catch (...) { note("Centroid"); }

	// Flux
	try {
		static const char* normNames[] = { "L1", "L2" };
		myFlux = AlgorithmFactory::create("Flux",
			"halfRectify", cfg.fluxHalfRect,
			"norm",        std::string(normNames[std::clamp(cfg.fluxNorm, 0, 1)]));
	} catch (...) { note("Flux"); }

	// RollOff
	try {
		myRollOff = AlgorithmFactory::create("RollOff",
			"sampleRate", sr,
			"cutoff",     static_cast<Real>(cfg.rolloffCutoff));
	} catch (...) { note("RollOff"); }

	// SpectralContrast
	try {
		mySpectralContrast = AlgorithmFactory::create("SpectralContrast",
			"numberBands", cfg.contrastBands,
			"sampleRate",  sr,
			"frameSize",   (cfg.specBins - 1) * 2);
	} catch (...) { note("SpectralContrast"); }

	// HFC
	try {
		static const char* hfcNames[] = { "Masri", "Jensen", "Brossier" };
		myHfc = AlgorithmFactory::create("HFC",
			"sampleRate", sr,
			"type",       std::string(hfcNames[std::clamp(cfg.hfcType, 0, 2)]));
	} catch (...) { note("HFC"); }

	// SpectralComplexity
	try {
		mySpectralComplexity = AlgorithmFactory::create("SpectralComplexity",
			"sampleRate",         sr,
			"magnitudeThreshold", static_cast<Real>(cfg.complexityThresh));
	} catch (...) { note("SpectralComplexity"); }

	// MelBands
	try {
		myMelBandsAlgo = AlgorithmFactory::create("MelBands",
			"inputSize",          cfg.specBins,
			"numberBands",        cfg.melBandCount,
			"sampleRate",         sr,
			"type",               std::string("power"),
			"lowFrequencyBound",  static_cast<Real>(melLo),
			"highFrequencyBound", static_cast<Real>(melHi));
	} catch (...) { note("MelBands"); }

	myConfigWarning = failed.empty()
		? std::string()
		: "Feature config failed (check FFT size / frequency bounds): " + failed;
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
	if (!frameProc.processAllFrames(audio.data.data(), audio.numSamples,
	                                &cancelFlag))
	{
		result.error = "Cancelled";
		return result;
	}

	const int numFrames = frameProc.numFrames();
	const int specBins  = frameProc.specBins();

	if (numFrames == 0)
	{
		result.warning = "Audio too short for given FFT size";
		result.success  = false;
		return result;
	}

	result.warning = batchFramingWarning(params.fftSize, params.hopSize);

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
		{
			// Mirror the RT naming exactly (rebuildChannelNames): the same
			// Melfreqnames setting must yield the same channel names in both
			// modes, or mode flips silently break downstream selects/exports
			if (params.melFreqNames)
			{
				const double lowFreq  = static_cast<double>(params.melLowFreq);
				const double highFreq = static_cast<double>(params.melHighFreq);
				const double melLow   = 1127.01048 * std::log(1.0 + lowFreq  / 700.0);
				const double melHigh  = 1127.01048 * std::log(1.0 + highFreq / 700.0);

				std::vector<int> edges(static_cast<size_t>(params.melBandCount) + 2);
				for (int i = 0; i < params.melBandCount + 2; ++i)
				{
					const double mel = melLow
						+ (melHigh - melLow) * i / (params.melBandCount + 1);
					edges[static_cast<size_t>(i)] =
						static_cast<int>(700.0 * (std::exp(mel / 1127.01048) - 1.0));
				}
				for (int i = 0; i < params.melBandCount; ++i)
					names.push_back("mel" + std::to_string(i)
						+ "_" + std::to_string(edges[static_cast<size_t>(i)])
						+ "_" + std::to_string(edges[static_cast<size_t>(i) + 2]));
			}
			else
			{
				for (int i = 0; i < params.melBandCount; ++i)
					names.push_back("mel" + std::to_string(i));
			}
		}
	}

	const int numCh = static_cast<int>(result.channelNames.size());
	result.cache.assign(numCh, std::vector<float>(numFrames, 0.0f));

	// Create Essentia algorithms locally
	const Real sr = static_cast<Real>(sampleRate);

	float mfccLo = params.mfccLowFreq, mfccHi = params.mfccHighFreq;
	clampFreqBounds(mfccLo, mfccHi, sampleRate);
	float melLo = params.melLowFreq, melHi = params.melHighFreq;
	clampFreqBounds(melLo, melHi, sampleRate);

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
			"lowFrequencyBound",  static_cast<Real>(mfccLo),
			"highFrequencyBound", static_cast<Real>(mfccHi));

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
	{
		static const char* hfcNames[] = { "Masri", "Jensen", "Brossier" };
		algoHfc = AlgorithmFactory::create("HFC",
			"sampleRate", sr,
			"type",       std::string(hfcNames[std::clamp(params.hfcType, 0, 2)]));
	}

	if (params.enableComplexity)
		algoComplexity = AlgorithmFactory::create("SpectralComplexity",
			"sampleRate",         sr,
			"magnitudeThreshold", static_cast<Real>(params.complexThresh));

	if (params.enableMel)
		algoMelBands = AlgorithmFactory::create("MelBands",
			"inputSize",          specBins,
			"numberBands",        params.melBandCount,
			"sampleRate",         sr,
			"type",               std::string("power"),
			"lowFrequencyBound",  static_cast<Real>(melLo),
			"highFrequencyBound", static_cast<Real>(melHi));

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

	// Bind I/O once before loop (Essentia set() just stores references)
	// We use a dummy spectrum ref for input binding; the actual spectrum
	// vector from frameProc is stable in memory, so we rebind input per frame.
	if (algoMfcc)
	{
		algoMfcc->output("mfcc").set(mfccCoeffs);
		algoMfcc->output("bands").set(mfccBands);
	}
	if (algoCentroid)
		algoCentroid->output("centroid").set(centroidVal);
	if (algoFlux)
		algoFlux->output("flux").set(fluxVal);
	if (algoRollOff)
		algoRollOff->output("rollOff").set(rollOffVal);
	if (algoSpectralContrast)
	{
		algoSpectralContrast->output("spectralContrast").set(contrastVals);
		algoSpectralContrast->output("spectralValley").set(contrastValleys);
	}
	if (algoHfc)
		algoHfc->output("hfc").set(hfcVal);
	if (algoComplexity)
		algoComplexity->output("spectralComplexity").set(complexityVal);
	if (algoMelBands)
		algoMelBands->output("bands").set(melBandVals);

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
					algoCentroid->compute();
				} catch (...) { centroidVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(centroidVal);
			}

			// Flux
			if (algoFlux)
			{
				try {
					algoFlux->input("spectrum").set(spectrum);
					algoFlux->compute();
				} catch (...) { fluxVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(fluxVal);
			}

			// RollOff
			if (algoRollOff)
			{
				try {
					algoRollOff->input("spectrum").set(spectrum);
					algoRollOff->compute();
				} catch (...) { rollOffVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(rollOffVal);
			}

			// SpectralContrast
			if (algoSpectralContrast)
			{
				try {
					algoSpectralContrast->input("spectrum").set(spectrum);
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
					algoHfc->compute();
				} catch (...) { hfcVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(hfcVal);
			}

			// SpectralComplexity
			if (algoComplexity)
			{
				try {
					algoComplexity->input("spectrum").set(spectrum);
					algoComplexity->compute();
				} catch (...) { complexityVal = 0.0f; }
				if (ch < numCh) result.cache[ch++][f] = static_cast<float>(complexityVal);
			}

			// MelBands
			if (algoMelBands)
			{
				try {
					algoMelBands->input("spectrum").set(spectrum);
					algoMelBands->compute();
				} catch (...) { melBandVals.assign(params.melBandCount, 0.0f); }

				for (int i = 0; i < params.melBandCount && ch < numCh; ++i)
				{
					float val = (i < static_cast<int>(melBandVals.size()))
					            ? static_cast<float>(melBandVals[i]) : 0.0f;
					if (params.melLog)
						val = 10.0f * std::log10(std::max(val, 1e-10f));
					result.cache[ch++][f] = val;
				}
			}
		}
	}

	// PCA post-processing
	if (params.enablePca && numCh > 0)
	{
		if (cancelFlag.load(std::memory_order_relaxed))
			goto cleanup;

		const int nc = std::min(params.pcaComponents, numCh);

		std::vector<std::vector<float>> pcCache;
		std::vector<float> varRatios;
		PCAProcessor::computeBatchPCA(result.cache, numCh, numFrames, nc,
		                              pcCache, varRatios);

		for (int c = 0; c < nc; ++c)
		{
			result.channelNames.push_back("pc" + std::to_string(c));
			result.cache.push_back(std::move(pcCache[static_cast<size_t>(c)]));
		}

		if (params.pcaVariance)
		{
			for (int c = 0; c < nc; ++c)
			{
				result.channelNames.push_back("pc_var" + std::to_string(c));
				result.cache.emplace_back(
					static_cast<size_t>(numFrames),
					varRatios[static_cast<size_t>(c)]);
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
