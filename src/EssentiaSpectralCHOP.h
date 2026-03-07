#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Spectral.h"

#include <essentia/algorithmfactory.h>
#include <vector>
#include <string>

namespace EssentiaTD
{

/// EssentiaSpectralCHOP
///
/// Accepts 1 input from EssentiaCoreCHOP (spec_mag*, mel*, rms channels)
/// and outputs per-frame spectral descriptors:
///   mfcc0..mfccN, spectral_centroid, spectral_flux, spectral_rolloff,
///   spectral_contrast0..5, hfc, spectral_complexity
///
/// All output channels carry 1 sample (snapshot per cook).
class EssentiaSpectralCHOP : public TD::CHOP_CPlusPlusBase
{
public:
	EssentiaSpectralCHOP(const TD::OP_NodeInfo* info);
	~EssentiaSpectralCHOP() override;

	// TD overrides
	void    getGeneralInfo(TD::CHOP_GeneralInfo* ginfo,
	                       const TD::OP_Inputs* inputs,
	                       void* reserved) override;

	bool    getOutputInfo(TD::CHOP_OutputInfo* info,
	                      const TD::OP_Inputs* inputs,
	                      void* reserved) override;

	void    getChannelName(int32_t index,
	                       TD::OP_String* name,
	                       const TD::OP_Inputs* inputs,
	                       void* reserved) override;

	void    execute(TD::CHOP_Output* output,
	                const TD::OP_Inputs* inputs,
	                void* reserved) override;

	void    setupParameters(TD::OP_ParameterManager* manager,
	                        void* reserved) override;

	int32_t getNumInfoCHOPChans(void* reserved) override;
	void    getInfoCHOPChan(int32_t index,
	                        TD::OP_InfoCHOPChan* chan,
	                        void* reserved) override;

	void getWarningString(TD::OP_String* warning, void* reserved1) override;
	void getErrorString(TD::OP_String* error, void* reserved1) override;

private:
	// Algorithm lifecycle
	struct AlgoConfig {
		int specBins = 0;
		int mfccCount = 13;
		int melBandCount = 40;
		int contrastBands = 6;
		double sampleRate = 44100.0;
		float mfccLowFreq = 0.0f;
		float mfccHighFreq = 11000.0f;
		float rolloffCutoff = 0.85f;
		int hfcType = 0;
		bool fluxHalfRect = false;
		int fluxNorm = 1;
		float complexityThresh = 0.005f;
		float melLowFreq = 0.0f;
		float melHighFreq = 22050.0f;
	};
	void configureAlgorithms(const AlgoConfig& cfg);
	void releaseAlgorithms();

	// Per-frame processing — called from execute()
	void processFrame(const std::vector<float>& spectrum,
	                  bool enableMfcc,   int mfccCount,
	                  bool enableCentroid,
	                  bool enableFlux,
	                  bool enableRolloff,
	                  bool enableContrast,
	                  bool enableHfc,
	                  bool enableComplexity,
	                  bool enableMel,    int melBandCount);

	// Rebuild myChannelNames according to current feature flags
	void rebuildChannelNames(bool enableMfcc,   int mfccCount,
	                         bool enableCentroid,
	                         bool enableFlux,
	                         bool enableRolloff,
	                         bool enableContrast,
	                         bool enableHfc,
	                         bool enableComplexity,
	                         bool enableMel,    int melBandCount,
	                         bool melFreqNames, double sampleRate);

	// ---------------------------------------------------------------------------
	// State tracking (detect changes requiring algorithm rebuild)
	// ---------------------------------------------------------------------------
	AlgoConfig myCfg;   // current algorithm configuration

	// Previous feature-enable state (used to detect channel-count changes)
	bool myPrevEnableMfcc      = true;
	bool myPrevEnableCentroid  = true;
	bool myPrevEnableFlux      = true;
	bool myPrevEnableRolloff   = true;
	bool myPrevEnableContrast  = true;
	bool myPrevEnableHfc       = true;
	bool myPrevEnableComplexity= true;
	int  myPrevMfccCount       = 13;
	bool myPrevEnableMel       = true;
	int  myPrevMelBandCount    = 40;
	bool myPrevMelFreqNames    = false;
	int  myPrevContrastBands   = 6;

	// ---------------------------------------------------------------------------
	// Essentia algorithm instances (owned; deleted in releaseAlgorithms)
	// ---------------------------------------------------------------------------
	essentia::standard::Algorithm* myMfcc             = nullptr;
	essentia::standard::Algorithm* myCentroid         = nullptr;
	essentia::standard::Algorithm* myFlux             = nullptr;
	essentia::standard::Algorithm* myRollOff          = nullptr;
	essentia::standard::Algorithm* mySpectralContrast = nullptr;
	essentia::standard::Algorithm* myHfc              = nullptr;
	essentia::standard::Algorithm* mySpectralComplexity = nullptr;
	essentia::standard::Algorithm* myMelBandsAlgo     = nullptr;

	// ---------------------------------------------------------------------------
	// Pre-allocated output buffers (filled by processFrame, read by execute)
	// ---------------------------------------------------------------------------
	std::vector<essentia::Real> myMfccCoeffs;    // size = mfccCount
	std::vector<essentia::Real> myMfccBands;     // internal — not exposed
	std::vector<essentia::Real> myMelBandValues; // size = melBandCount
	essentia::Real              myCentroidVal    = 0.0f;
	essentia::Real              myFluxVal        = 0.0f;
	essentia::Real              myRollOffVal     = 0.0f;
	std::vector<essentia::Real> myContrastValues; // size = contrastBands
	std::vector<essentia::Real> myContrastValleys; // internal
	essentia::Real              myHfcVal         = 0.0f;
	essentia::Real              myComplexityVal  = 0.0f;

	// Intermediate: essentia::Real copy of the input spectrum
	std::vector<essentia::Real> mySpectrumReal;

	// Channel name list (rebuilt when features change)
	std::vector<std::string> myChannelNames;

	// Init state
	bool myInitOk = false;

	// Diagnostics
	std::string myError;
	std::string myWarning;

	// Current number of spectral contrast bands (runtime, default 6)
	int myContrastBands = 6;
};

} // namespace EssentiaTD
