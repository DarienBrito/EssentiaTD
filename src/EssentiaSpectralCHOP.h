#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Shared/UnifiedCHOPBase.h"
#include "Shared/RTFrameProcessor.h"
#include "Shared/Utils.h"
#include "Shared/PCAProcessor.h"
#include "Parameters_Spectral.h"

#include <essentia/algorithmfactory.h>
#include <vector>
#include <string>
#include <atomic>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Plain-data snapshot of all spectral parameters for the async worker.
// Captured on the main thread; passed by value into the worker lambda.
// ---------------------------------------------------------------------------

struct BatchSpectralParams
{
	// FFT / framing
	int         fftSize       = 1024;
	int         hopSize       = 512;
	std::string windowType    = "hann";
	int         zeroPadFactor = 0;   // 0=none, 1=half FFT, 2=full FFT

	// MFCC
	bool  enableMfcc    = true;
	int   mfccCount     = 13;
	float mfccLowFreq   = 0.0f;
	float mfccHighFreq  = 11000.0f;

	// Centroid / Flux / Rolloff
	bool  enableCentroid  = true;
	bool  enableFlux      = false;
	bool  fluxHalfRect    = false;
	int   fluxNorm        = 1;   // 0=L1, 1=L2
	bool  enableRolloff   = false;
	float rolloffCutoff   = 0.85f;

	// Spectral Contrast
	bool enableContrast  = false;
	int  contrastBands   = 6;

	// HFC
	bool enableHfc = true;
	int  hfcType   = 0;   // 0=Masri, 1=Jensen, 2=Brossier

	// Spectral Complexity
	bool  enableComplexity = true;
	float complexThresh    = 0.005f;

	// Mel Bands
	bool  enableMel    = true;
	int   melBandCount = 40;
	float melLowFreq   = 0.0f;
	float melHighFreq  = 22050.0f;
	bool  melLog       = false;
	bool  melFreqNames = false;  // batch names must match RT (mode-consistent)

	// PCA
	bool enablePca     = false;
	int  pcaComponents = 3;
	bool pcaVariance   = false;
};

// ---------------------------------------------------------------------------
// EssentiaSpectralCHOP — unified real-time + batch spectral analysis CHOP
// ---------------------------------------------------------------------------

class EssentiaSpectralCHOP : public UnifiedCHOPBase<EssentiaSpectralCHOP>
{
	friend class UnifiedCHOPBase<EssentiaSpectralCHOP>;

public:
	explicit EssentiaSpectralCHOP(const TD::OP_NodeInfo* info)
		: UnifiedCHOPBase(info)
	{
		// Pre-size contrast buffers so they are never empty
		myContrastValues.resize(6, 0.0f);
		myContrastValleys.resize(6, 0.0f);
	}

	~EssentiaSpectralCHOP() override
	{
		releaseAlgorithms();
		// Base destructor calls myRunner.cancelAndWait()
	}

	// ----- Static async worker (no access to `this`) -----

	static AsyncBatchResult computeBatchAsync(
		const AudioSnapshot&       audio,
		const BatchSpectralParams& params,
		const std::atomic<bool>&   cancelFlag,
		std::atomic<float>&        progress);

private:
	// ----- UnifiedCHOPBase CRTP hooks -----

	bool    getOutputInfoImpl(TD::CHOP_OutputInfo* info,
	                          const TD::OP_Inputs* inputs,
	                          bool isBatch);

	void    getChannelNameImpl(int32_t index,
	                           TD::OP_String* name,
	                           const TD::OP_Inputs* inputs);

	void    executeRealtimeImpl(TD::CHOP_Output* output,
	                            const TD::OP_Inputs* inputs);

	void    snapshotAndLaunch(AudioSnapshot audio,
	                          const TD::OP_Inputs* inputs);

	void    onResultCollected(AsyncBatchResult& result);

	void    setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	// v2.0: reject legacy spectrum wiring with a migration error (both modes)
	const char* inputContractErrorImpl(const TD::OP_CHOPInput* in)
	{
		return spectrumInputContractError(in);
	}

	// ----- RT helpers -----

	/// Algorithm lifecycle
	struct AlgoConfig {
		int    specBins         = 0;
		int    mfccCount        = 13;
		int    melBandCount     = 40;
		int    contrastBands    = 6;
		double sampleRate       = 44100.0;
		float  mfccLowFreq      = 0.0f;
		float  mfccHighFreq     = 11000.0f;
		float  rolloffCutoff    = 0.85f;
		int    hfcType          = 0;
		bool   fluxHalfRect     = false;
		int    fluxNorm         = 1;
		float  complexityThresh = 0.005f;
		float  melLowFreq       = 0.0f;
		float  melHighFreq      = 22050.0f;
	};

	void configureAlgorithms(const AlgoConfig& cfg);
	void releaseAlgorithms();

	/// Per-frame processing — called from executeRealtimeImpl
	void processFrame(const std::vector<float>& spectrum,
	                  bool enableMfcc,      int  mfccCount,
	                  bool enableCentroid,
	                  bool enableFlux,
	                  bool enableRolloff,
	                  bool enableContrast,
	                  bool enableHfc,
	                  bool enableComplexity,
	                  bool enableMel,       int  melBandCount);

	/// Rebuild myChannelNames according to current feature flags
	void rebuildChannelNames(bool enableMfcc,      int  mfccCount,
	                         bool enableCentroid,
	                         bool enableFlux,
	                         bool enableRolloff,
	                         bool enableContrast,
	                         bool enableHfc,
	                         bool enableComplexity,
	                         bool enableMel,       int  melBandCount,
	                         bool melFreqNames,    double sampleRate);

	// ----- RT: internal FFT (v2.0 — input is raw audio in both modes) -----

	RTFrameProcessor myFrameProc;
	int         myRtFftSize = 0;      // size myFrameProc is built for
	std::string myRtWindowType;

	// ----- RT state: algorithm configuration tracking -----

	AlgoConfig myCfg;

	/// Names of features whose algorithm construction failed on the last
	/// reconfigure — surfaced as a persistent warning each cook
	std::string myConfigWarning;

	bool myPrevEnableMfcc       = true;
	bool myPrevEnableCentroid   = true;
	bool myPrevEnableFlux       = false;
	bool myPrevEnableRolloff    = false;
	bool myPrevEnableContrast   = false;
	bool myPrevEnableHfc        = true;
	bool myPrevEnableComplexity = true;
	int  myPrevMfccCount        = 13;
	bool myPrevEnableMel        = true;
	int  myPrevMelBandCount     = 40;
	bool myPrevMelFreqNames     = false;
	int  myPrevContrastBands    = 6;

	// ----- Essentia algorithm instances (owned; deleted in releaseAlgorithms) -----

	essentia::standard::Algorithm* myMfcc               = nullptr;
	essentia::standard::Algorithm* myCentroid           = nullptr;
	essentia::standard::Algorithm* myFlux               = nullptr;
	essentia::standard::Algorithm* myRollOff            = nullptr;
	essentia::standard::Algorithm* mySpectralContrast   = nullptr;
	essentia::standard::Algorithm* myHfc                = nullptr;
	essentia::standard::Algorithm* mySpectralComplexity = nullptr;
	essentia::standard::Algorithm* myMelBandsAlgo       = nullptr;

	// ----- Pre-allocated output buffers (RT path) -----

	std::vector<essentia::Real> myMfccCoeffs;     // size = mfccCount
	std::vector<essentia::Real> myMfccBands;      // internal
	std::vector<essentia::Real> myMelBandValues;  // size = melBandCount
	essentia::Real              myCentroidVal     = 0.0f;
	essentia::Real              myFluxVal         = 0.0f;
	essentia::Real              myRollOffVal      = 0.0f;
	std::vector<essentia::Real> myContrastValues; // size = contrastBands
	std::vector<essentia::Real> myContrastValleys;
	essentia::Real              myHfcVal          = 0.0f;
	essentia::Real              myComplexityVal   = 0.0f;

	/// Intermediate: essentia::Real copy of the input spectrum
	std::vector<essentia::Real> mySpectrumReal;

	/// Channel name list (rebuilt when features change; also moved-into from batch result)
	std::vector<std::string> myChannelNames;

	/// Current number of spectral contrast bands (runtime, default 6)
	int myContrastBands = 6;

	// ----- PCA state -----

	PCAProcessor myPca;
	int  myPcaUpdateCounter  = 0;
	bool myPrevEnablePca     = false;
	int  myPrevPcaComponents = 3;
	bool myPrevPcaVariance   = false;

	std::vector<float> myPcaFeatureBuffer;
	std::vector<float> myPcaProjectBuffer;
};

} // namespace EssentiaTD
