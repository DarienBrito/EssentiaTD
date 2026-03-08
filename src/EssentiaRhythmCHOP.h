#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unified Rhythm CHOP — real-time onset/BPM analysis (Mode=Realtime)
// or full-file batch rhythm extraction (Mode=Batch).
//
// Real-time path:
//   Consumes a spectrum CHOP produced by EssentiaSpectrumCHOP and runs
//   OnsetDetection + autocorrelation BPM estimation with harmonic summation,
//   optional Gaussian tempo prior, median filter, and EMA smoothing.
//
// Batch path:
//   Runs RhythmExtractor2013 (with autocorrelation fallback) over the entire
//   input buffer using BatchFrameProcessor for windowing + FFT.
//
// Output channels (6) — same order in both modes:
//   onset           — onset trigger (0 or 1, resets to 0 next frame / next analysis frame)
//   onset_strength  — raw onset detection function value
//   bpm             — tempo estimate in BPM
//   beat            — beat trigger (0 or 1)
//   beat_phase      — 0-1 position within the current beat cycle
//   beat_confidence — confidence of the BPM estimate (0-1)

#include "Shared/UnifiedCHOPBase.h"
#include "Parameters_Rhythm.h"

#include <essentia/algorithmfactory.h>

#include <array>
#include <string>
#include <vector>

namespace EssentiaTD
{

class EssentiaRhythmCHOP : public UnifiedCHOPBase<EssentiaRhythmCHOP>
{
	friend class UnifiedCHOPBase<EssentiaRhythmCHOP>;

public:
	explicit EssentiaRhythmCHOP(const TD::OP_NodeInfo* info);
	~EssentiaRhythmCHOP() override;

	static constexpr int kNumOutputChannels = 6;

	// ---- Async worker (static so it can be captured by value in a lambda) ----

	/// POD snapshot of all batch-relevant parameters.
	struct BatchRhythmParams
	{
		int         fftSize;
		int         hopSize;
		std::string windowType;
		int         zeroPad;
		int         rhythmMethod;
		int         onsetMethodIdx;
		float       sensitivity;
		int         bpmMin;
		int         bpmMax;
	};

	/// Async worker — called on a background thread by the runner.
	/// Returns result with channels reordered to match RT channel order:
	///   cache[0] = onset (binary), cache[1] = onset_strength,
	///   cache[2] = bpm,            cache[3] = beat (binary),
	///   cache[4] = beat_phase,     cache[5] = beat_confidence.
	static AsyncBatchResult computeBatchAsync(
		const AudioSnapshot&       audio,
		const BatchRhythmParams&   params,
		const std::atomic<bool>&   cancelFlag,
		std::atomic<float>&        progress);

private:
	// ---- UnifiedCHOPBase CRTP hooks ----

	bool    getOutputInfoImpl(TD::CHOP_OutputInfo* info,
	                          const TD::OP_Inputs* inputs, bool isBatch);

	void    getChannelNameImpl(int32_t index, TD::OP_String* name,
	                           const TD::OP_Inputs* inputs);

	void    executeRealtimeImpl(TD::CHOP_Output* output,
	                            const TD::OP_Inputs* inputs);

	void    snapshotAndLaunch(AudioSnapshot audio,
	                          const TD::OP_Inputs* inputs);

	void    setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	/// Extract batch-specific fields after a successful result is collected.
	void    onResultCollected(AsyncBatchResult& result);

	// ---- RT: algorithm lifecycle ----

	/// (Re-)create OnsetDetection for the given spectrum size, method, and sample rate.
	void configureOnsetDetection(int specSize, const char* method, double sampleRate);
	void releaseAlgorithms();

	// ---- RT: BPM helpers ----

	/// Append one onset-strength sample to the circular history buffer.
	void pushOnsetStrength(float value);

	/// Compute autocorrelation-based BPM from myOnsetHistory.
	/// Uses harmonic summation and optional Gaussian tempo prior.
	/// Returns 0 if not enough history data is available.
	float computeAutocorrBpm(int bpmMin, int bpmMax,
	                         double sampleRate, int hopSize,
	                         bool tempoBias, float biasCenter) const;

	/// Advance beat phase by one frame and fire beat trigger when phase wraps.
	/// Also clamps/commits BPM and confidence to the output members.
	void updateBeatPhase(int bpmMin, int bpmMax);

	/// Write all myOut* members into the CHOP_Output channels.
	void writeOutputs(TD::CHOP_Output* output);

	// ---- RT state: Essentia ----

	essentia::standard::Algorithm* myOnsetDetection = nullptr;

	// Working buffers (pre-allocated to avoid per-frame heap allocation)
	std::vector<essentia::Real> mySpectrumBuf;
	std::vector<essentia::Real> myPhaseBuf;
	essentia::Real              myOnsetValue = 0.0f;

	// Configuration tracking (reconfigure when any of these change)
	int         mySpecSize     = 0;
	double      mySampleRate   = 0.0;
	int         myHopSize      = 0;    ///< estimated hop in samples (derived from fps)
	std::string myCurrentMethod;

	// ---- RT state: onset history circular buffer ----
	// ~8 s at 44100/1024 hop ≈ 344 frames; 512 slots provide comfortable headroom.
	static constexpr int kOnsetHistorySize = 512;
	std::vector<float>   myOnsetHistory;
	int                  myOnsetWritePos  = 0;
	int                  myOnsetFillCount = 0;

	// ---- RT state: BPM estimation ----

	float         mySmoothedBpm    = 120.0f;  ///< EMA-filtered BPM output
	float         myBeatPhase      = 0.0f;    ///< 0-1 within beat cycle
	mutable float myBeatConfidence = 0.0f;    ///< normalised autocorrelation peak

	// Median filter over recent raw BPM estimates (removes octave-error spikes)
	static constexpr int kBpmMedianSize = 5;
	std::array<float, kBpmMedianSize> myBpmMedianBuf = {};
	int myBpmMedianPos  = 0;
	int myBpmMedianFill = 0;

	// Adaptive onset normalisation
	float myRunningMax = 1e-4f;

	// ---- RT output values ----
	float myOutOnset          = 0.0f;
	float myOutOnsetStrength  = 0.0f;
	float myOutBpm            = 0.0f;
	float myOutBeat           = 0.0f;
	float myOutBeatPhase      = 0.0f;
	float myOutBeatConfidence = 0.0f;

	// Frame counter used to derive a coarse fps estimate
	uint64_t myFrameCount  = 0;
	double   myFpsEstimate = 60.0;

	// ---- Batch-specific state ----
	float myDetectedBpm  = 0.0f;
	bool  myUsedAutocorr = false;
};

} // namespace EssentiaTD
