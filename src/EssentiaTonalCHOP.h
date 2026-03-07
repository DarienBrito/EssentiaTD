#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Tonal.h"

#include <essentia/algorithmfactory.h>

#include <deque>
#include <string>
#include <vector>

namespace EssentiaTD
{

/// EssentiaTonalCHOP
///
/// Consumes the FFT magnitude spectrum produced by EssentiaCoreCHOP
/// (channels "spec_mag0" .. "spec_magN") and runs a tonal analysis
/// pipeline that outputs pitch, HPCP chroma, key, dissonance, and
/// inharmonicity — all as 1-sample channels.
///
/// Algorithm chain:
///   spectrum -> PitchYinFFT          -> pitch, pitch_confidence
///   spectrum -> SpectralPeaks        -> peak frequencies & magnitudes
///            -> HPCP                 -> hpcp0..hpcpN
///            -> Key                  -> key, key_scale, key_strength
///            -> Dissonance           -> dissonance
///            -> Inharmonicity        -> inharmonicity
class EssentiaTonalCHOP : public TD::CHOP_CPlusPlusBase
{
public:
	explicit EssentiaTonalCHOP(const TD::OP_NodeInfo* info);
	~EssentiaTonalCHOP() override;

	// -- Required TD overrides --

	void getGeneralInfo(TD::CHOP_GeneralInfo* ginfo,
	                    const TD::OP_Inputs*, void*) override;

	bool getOutputInfo(TD::CHOP_OutputInfo* info,
	                   const TD::OP_Inputs* inputs, void*) override;

	void getChannelName(int32_t index, TD::OP_String* name,
	                    const TD::OP_Inputs*, void*) override;

	void execute(TD::CHOP_Output* output,
	             const TD::OP_Inputs* inputs, void*) override;

	void setupParameters(TD::OP_ParameterManager* manager, void*) override;

	// -- Info CHOP --

	int32_t getNumInfoCHOPChans(void*) override;
	void    getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* chan,
	                        void*) override;

	// -- Diagnostic strings --

	void getWarningString(TD::OP_String* warning, void* reserved1) override;
	void getErrorString(TD::OP_String* error, void* reserved1) override;

private:
	// Algorithm tuning config
	struct TonalConfig {
		int specBins = 0;
		int hpcpSize = 12;
		double sampleRate = 44100.0;
		int pitchAlgo = 0;
		float pitchMinFreq = 20.0f;
		float pitchMaxFreq = 22050.0f;
		float pitchTolerance = 1.0f;
		float peakThreshold = 0.0f;
		float peakMaxFreq = 5000.0f;
		int hpcpHarmonics = 0;
		float referenceFreq = 440.0f;
		bool hpcpNonLinear = false;
		int hpcpNormalized = 0; // 0=unitMax, 1=unitSum, 2=none
		int keyProfile = 0;
	};

	void configureAlgorithms(const TonalConfig& cfg);
	void releaseAlgorithms();

	// Rebuild myChannelNames to reflect current feature flags + HPCP size.
	void rebuildChannelNames(bool pitch, bool pitchNote, bool hpcp, int hpcpSize,
	                         bool key, bool dissonance, bool inharmonicity,
	                         bool musicalLabels);

	// -- Cached configuration --
	TonalConfig myTCfg;

	// -- Feature enable flags (cached to detect change) --
	bool myEnablePitch         = true;
	bool myEnableHpcp          = true;
	bool myEnableKey           = true;
	bool myEnableDissonance    = true;
	bool myEnableInharmonicity = true;
	bool myMusicalLabels       = false;
	bool myEnablePitchNote     = false;

	// -- Essentia algorithm instances (owned, deleted by releaseAlgorithms) --
	essentia::standard::Algorithm* myPitchYinFFT    = nullptr;
	essentia::standard::Algorithm* mySpectralPeaks  = nullptr;
	essentia::standard::Algorithm* myHpcp           = nullptr;
	essentia::standard::Algorithm* myKey            = nullptr;
	essentia::standard::Algorithm* myDissonance     = nullptr;
	essentia::standard::Algorithm* myInharmonicity  = nullptr;

	// -- Pre-allocated analysis buffers --
	std::vector<essentia::Real> mySpectrumBuf;       // input spectrum (float->Real copy)
	std::vector<essentia::Real> myPeakFreqs;         // SpectralPeaks output
	std::vector<essentia::Real> myPeakMags;          // SpectralPeaks output
	std::vector<essentia::Real> myHpcpBuf;           // HPCP output
	std::vector<essentia::Real> myKeyPcpBuf;         // stable buffer for Key input

	// -- Last analysis results (written by execute, read by getChannelName) --
	essentia::Real myPitchHz          = 0.0f;
	essentia::Real myPitchConfidence  = 0.0f;
	essentia::Real myKeyStrength      = 0.0f;
	essentia::Real myFirstToSecondRelativeStrength = 0.0f;
	essentia::Real myDissonanceVal    = 0.0f;
	essentia::Real myInharmonicityVal = 0.0f;
	std::string    myKeyStr;    // raw string from Key algorithm ("C", "C#", …)
	std::string    myScaleStr;  // "major" or "minor"

	// -- Smoothing state (EMA) --
	float mySmoothedPitch         = 0.0f;
	float mySmoothedPitchConf     = 0.0f;
	std::vector<float> mySmoothedHpcp;
	float mySmoothedDissonance    = 0.0f;
	float mySmoothedInharmonicity = 0.0f;

	// -- HPCP accumulator for averaged Key detection --
	std::deque<std::vector<essentia::Real>> myHpcpAccum;

	// -- Output channel name list (rebuilt on reconfigure) --
	std::vector<std::string> myChannelNames;

	// -- Init state --
	bool myInitOk = false;

	// -- Diagnostic strings --
	std::string myError;
	std::string myWarning;
};

} // namespace EssentiaTD
