#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Shared/UnifiedCHOPBase.h"
#include "Shared/RTFrameProcessor.h"
#include "Shared/Utils.h"
#include "Parameters_Tonal.h"

#include <essentia/algorithmfactory.h>

#include <atomic>
#include <deque>
#include <string>
#include <tuple>
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

	// Every field, so a cached result can tell when the node's parameters moved
	// past it. A field missing here is a parameter the staleness warning cannot see.
	auto tie() const
	{
		return std::tie(fftSize, hopSize, windowType, zeroPad,
		                enablePitch, enablePitchNote, enableHpcp, hpcpSize,
		                enableKey, keyMode, keyWindowSize,
		                enableDiss, enableInharm, musicalLabels,
		                pitchAlgo, pitchMinFreq, pitchMaxFreq, pitchTolerance,
		                peakThreshold, peakMaxFreq, hpcpHarmonics, referenceFreq,
		                hpcpNonLinear, hpcpNormalized, keyProfile);
	}
	bool operator==(const BatchTonalParams& o) const { return tie() == o.tie(); }
	bool operator!=(const BatchTonalParams& o) const { return !(*this == o); }
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

	/// Single source of truth for the batch parameter snapshot: the launch
	/// and the staleness check must read the same values or the warning lies.
	static BatchTonalParams evalBatchParams(const TD::OP_Inputs* inputs,
	                                        double sampleRate);
	bool    batchParamsStaleImpl(const TD::OP_Inputs* inputs);
	BatchTonalParams myLaunchedParams;
	bool             myHasLaunchedParams = false;

	void    onResultCollected(AsyncBatchResult& result);

	void    setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	// v2.0: reject legacy spectrum wiring with a migration error (both modes)
	const char* inputContractErrorImpl(const TD::OP_CHOPInput* in)
	{
		return spectrumInputContractError(in);
	}

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

	// True when at least one enabled feature still has every algorithm it needs
	bool anyEnabledFeatureLive(bool pitch, bool hpcp, bool key,
	                           bool dissonance, bool inharmonicity) const;

	/// Channel names captured when the batch result was computed (see
	/// getChannelNameImpl). Separate from the live list on purpose.
	std::vector<std::string> myBatchChannelNames;

	// Rebuild channel names from current feature flags + HPCP size (RT path)
	void rebuildChannelNames(bool pitch, bool pitchNote, bool hpcp, int hpcpSize,
	                         bool key, bool dissonance, bool inharmonicity,
	                         bool musicalLabels);

	// --- RT: internal FFT (v2.0 — input is raw audio in both modes) ---
	RTFrameProcessor myFrameProc;
	int         myRtFftSize = 0;      // resolved size myFrameProc is built for
	std::string myRtWindowType;

	// --- RT: Cached configuration ---
	TonalConfig myTCfg;

	/// Algorithms whose construction failed on the last reconfigure, with
	/// Essentia's reason for each. Pre-formed as the warning text because it is
	/// re-emitted every cook; empty when every algorithm was built.
	std::string myConfigWarning;

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

	// Key stage runs its own whitening + chroma, pinned to KeyExtractor's
	// configuration (keyextractor.cpp:63-68, :112-123). Separate from myHpcp so
	// the user-facing hpcp_* channels keep their own parameters.
	essentia::standard::Algorithm* mySpectralWhitening = nullptr;
	essentia::standard::Algorithm* myKeyHpcp           = nullptr;

	// --- RT: Pre-allocated analysis buffers ---
	std::vector<essentia::Real> mySpectrumBuf;
	std::vector<essentia::Real> myPeakFreqs;
	std::vector<essentia::Real> myPeakMags;
	std::vector<essentia::Real> myHpcpBuf;
	std::vector<essentia::Real> myKeyPcpBuf;
	std::vector<essentia::Real> myWhitenedMags;
	std::vector<essentia::Real> myKeyHpcpBuf;

	// --- RT: Scratch buffers for re-sorting SpectralPeaks by ascending freq ---
	std::vector<std::size_t>    mySortIdx;
	std::vector<essentia::Real> mySortedF;
	std::vector<essentia::Real> mySortedM;

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
