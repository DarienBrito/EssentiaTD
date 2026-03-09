#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Shared/UnifiedCHOPBase.h"
#include "Parameters_Tonal.h"

#include <essentia/algorithmfactory.h>

#include <atomic>
#include <deque>
#include <string>
#include <vector>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Plain-data snapshot of all parameters needed by the async batch worker.
// Captured by value before the background thread is launched so the worker
// never touches TD::OP_Inputs*.
// ---------------------------------------------------------------------------

struct BatchTonalParams
{
	int         fftSize         = 1024;
	int         hopSize         = 512;
	std::string windowType      = "hann";
	int         zeroPad         = 0;        // absolute zero-pad samples

	bool        enablePitch     = true;
	bool        enablePitchNote = false;
	bool        enableHpcp      = true;
	int         hpcpSize        = 12;
	bool        enableKey       = true;
	int         keyMode         = 0;        // 0 = global, 1 = windowed
	int         keyWindowSize   = 8;
	bool        enableDiss      = false;
	bool        enableInharm    = false;
	bool        musicalLabels   = true;

	int         pitchAlgo       = 0;
	float       pitchMinFreq    = 20.0f;
	float       pitchMaxFreq    = 22050.0f;
	float       pitchTolerance  = 1.0f;
	float       peakThreshold   = 0.00001f;
	float       peakMaxFreq     = 3500.0f;
	int         hpcpHarmonics   = 0;
	float       referenceFreq   = 440.0f;
	bool        hpcpNonLinear   = false;
	int         hpcpNormalized  = 0;
	int         keyProfile      = 0;
};

// ---------------------------------------------------------------------------

class EssentiaTonalCHOP : public UnifiedCHOPBase<EssentiaTonalCHOP>
{
	friend class UnifiedCHOPBase<EssentiaTonalCHOP>;

public:
	explicit EssentiaTonalCHOP(const TD::OP_NodeInfo* info);
	~EssentiaTonalCHOP() override;

	// Static async batch worker — no `this` capture
	static AsyncBatchResult computeBatchAsync(
		const AudioSnapshot&       audio,
		const BatchTonalParams&    params,
		const std::atomic<bool>&   cancelFlag,
		std::atomic<float>&        progress);

private:
	// --- CRTP hooks (called by UnifiedCHOPBase) ---

	bool    getOutputInfoImpl(TD::CHOP_OutputInfo* info,
	                          const TD::OP_Inputs* inputs,
	                          bool isBatch);

	void    getChannelNameImpl(int32_t index, TD::OP_String* name,
	                           const TD::OP_Inputs* inputs);

	void    executeRealtimeImpl(TD::CHOP_Output* output,
	                            const TD::OP_Inputs* inputs);

	void    snapshotAndLaunch(AudioSnapshot audio,
	                          const TD::OP_Inputs* inputs);

	void    onResultCollected(AsyncBatchResult& result);

	void    setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	// --- RT private helpers ---

	// Algorithm tuning config (RT)
	struct TonalConfig
	{
		int specBins = 0;
		int hpcpSize = 12;
		double sampleRate = 44100.0;
		int pitchAlgo = 0;
		float pitchMinFreq = 20.0f;
		float pitchMaxFreq = 22050.0f;
		float pitchTolerance = 1.0f;
		float peakThreshold = 0.00001f;
		float peakMaxFreq = 3500.0f;
		int hpcpHarmonics = 0;
		float referenceFreq = 440.0f;
		bool hpcpNonLinear = false;
		int hpcpNormalized = 0; // 0=unitMax, 1=unitSum, 2=none
		int keyProfile = 0;
	};

	void configureAlgorithms(const TonalConfig& cfg);
	void releaseAlgorithms();

	// Rebuild channel names from current feature flags + HPCP size (RT path)
	void rebuildChannelNames(bool pitch, bool pitchNote, bool hpcp, int hpcpSize,
	                         bool key, bool dissonance, bool inharmonicity,
	                         bool musicalLabels);

	// --- RT: Cached configuration ---
	TonalConfig myTCfg;

	// --- RT: Feature enable flags (cached to detect change) ---
	bool myEnablePitch         = true;
	bool myEnableHpcp          = true;
	bool myEnableKey           = true;
	bool myEnableDissonance    = true;
	bool myEnableInharmonicity = true;
	bool myMusicalLabels       = false;
	bool myEnablePitchNote     = false;

	// --- RT: Essentia algorithm instances (owned, deleted by releaseAlgorithms) ---
	essentia::standard::Algorithm* myPitchYinFFT    = nullptr;
	essentia::standard::Algorithm* mySpectralPeaks  = nullptr;
	essentia::standard::Algorithm* myHpcp           = nullptr;
	essentia::standard::Algorithm* myKey            = nullptr;
	essentia::standard::Algorithm* myDissonance     = nullptr;
	essentia::standard::Algorithm* myInharmonicity  = nullptr;

	// --- RT: Pre-allocated analysis buffers ---
	std::vector<essentia::Real> mySpectrumBuf;
	std::vector<essentia::Real> myPeakFreqs;
	std::vector<essentia::Real> myPeakMags;
	std::vector<essentia::Real> myHpcpBuf;
	std::vector<essentia::Real> myKeyPcpBuf;

	// --- RT: Last analysis results ---
	essentia::Real myPitchHz          = 0.0f;
	essentia::Real myPitchConfidence  = 0.0f;
	essentia::Real myKeyStrength      = 0.0f;
	essentia::Real myFirstToSecondRelativeStrength = 0.0f;
	essentia::Real myDissonanceVal    = 0.0f;
	essentia::Real myInharmonicityVal = 0.0f;
	std::string    myKeyStr;
	std::string    myScaleStr;

	// --- RT: Smoothing state (EMA) ---
	float mySmoothedPitch         = 0.0f;
	float mySmoothedPitchConf     = 0.0f;
	std::vector<float> mySmoothedHpcp;
	float mySmoothedDissonance    = 0.0f;
	float mySmoothedInharmonicity = 0.0f;

	// --- RT: HPCP accumulator for averaged Key detection ---
	std::deque<std::vector<essentia::Real>> myHpcpAccum;

	// --- Shared: Output channel name list ---
	std::vector<std::string> myChannelNames;
};

} // namespace EssentiaTD
