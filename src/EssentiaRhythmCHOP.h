#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unified Rhythm CHOP — real-time onset/BPM analysis (Mode=Realtime)
// or full-file batch rhythm extraction (Mode=Batch).
//
// Real-time path:
//   Consumes spectrum+phase from EssentiaSpectrumCHOP and runs
//   OnsetDetection (with real phase) or SuperFluxNovelty for onset strength,
//   Onsets-style adaptive thresholding for onset triggers, and
//   TempoTapDegara for BPM/beat estimation.
//
// Batch path:
//   Runs RhythmExtractor2013 (resampled to 44100 Hz, with autocorrelation
//   fallback) over the entire input buffer.  Onset detection uses Essentia's
//   Onsets algorithm for all onset methods (unified thresholding).
//
// Output channels (6) — same order in both modes:
//   onset           — onset trigger (0 or 1)
//   onset_strength  — raw onset detection function value
//   bpm             — tempo estimate in BPM
//   beat            — beat trigger (0 or 1)
//   beat_phase      — 0-1 position within the current beat cycle
//   beat_confidence — confidence of the BPM estimate (0-1)

#include "Shared/UnifiedCHOPBase.h"
#include "Parameters_Rhythm.h"

#include <essentia/algorithmfactory.h>

#include <array>
#include <deque>
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

	void    onResultCollected(AsyncBatchResult& result);

	// ---- RT: algorithm lifecycle ----

	/// (Re-)create onset algorithms for the given spectrum size and method.
	/// Index 0-4: OnsetDetection (hfc/complex/flux/melflux/rms)
	/// Index 5:   SuperFlux (TriangularBands + SuperFluxNovelty)
	void configureOnsetDetection(int specSize, int onsetMethodIdx, double sampleRate);
	void releaseAlgorithms();

	// ---- RT: helpers ----

	void pushOnsetStrength(float value);

	/// Onsets-style adaptive onset detection.
	bool detectOnset(float odfValue, float sensitivity);

	/// Run TempoTapDegara periodically on accumulated ODF buffer.
	/// getTimeInfo()->rate guarded against transient garbage values —
	/// one absurd rate poisons dt and every time-derived state after it.
	static double sanitizedFrameRate(const TD::OP_Inputs* inputs);

	void updateTempoEstimate(double frameRate, int bpmMin, int bpmMax);

	/// Advance beat phase, anchored to TempoTapDegara ticks when available.
	void updateBeatPhase(double frameRate, int bpmMin, int bpmMax);

	void writeOutputs(TD::CHOP_Output* output);

	// ---- RT state: Essentia algorithms ----

	essentia::standard::Algorithm* myOnsetDetection   = nullptr; // methods 0-4
	essentia::standard::Algorithm* myTriangularBands   = nullptr; // method 5
	essentia::standard::Algorithm* mySuperFluxNovelty  = nullptr; // method 5

	// Working buffers (pre-allocated, bound once in configureOnsetDetection)
	std::vector<essentia::Real> mySpectrumBuf;
	std::vector<essentia::Real> myPhaseBuf;
	essentia::Real              myOnsetValue = 0.0f;
	std::vector<essentia::Real> myBandsBuf;                       // TriangularBands output
	std::deque<std::vector<essentia::Real>> myBandsHistory;       // rolling buffer for SuperFlux

	// Configuration tracking
	int    mySpecSize          = 0;
	double mySampleRate        = 0.0;
	int    myCurrentMethodIdx  = -1;

	// ---- RT state: onset history circular buffer (feeds TempoTapDegara) ----
	static constexpr int kOnsetHistorySize = 512;
	std::vector<float>   myOnsetHistory;
	int                  myOnsetWritePos  = 0;
	int                  myOnsetFillCount = 0;

	// ---- RT state: Onsets-style adaptive threshold ----
	static constexpr int   kThresholdDelay   = 5;
	static constexpr float kAlpha            = 0.1f;
	// Silence gate is RELATIVE to the signal's own running ODF peak — an
	// absolute gate silently disables onset detection for methods whose ODF
	// scale sits below it (and never engages for high-scale methods)
	static constexpr float kSilenceRelative  = 0.01f;  // fraction of running peak
	static constexpr float kSilenceFloor     = 1e-6f;  // true digital silence
	float myOdfRunningPeak = 0.0f;
	std::array<float, kThresholdDelay> myOdfLookback = {};
	int   myOdfLookbackPos  = 0;
	int   myOdfLookbackFill = 0;
	float myDecayThreshold  = 0.0f;

	// ---- RT state: TempoTapDegara BPM estimation ----
	static constexpr int kTempoUpdateInterval = 90; // ~1.5 s at 60 fps
	int                  myTempoUpdateCounter = 0;
	float                myCurrentBpm         = 120.0f;
	float                myBeatConfidence     = 0.0f;
	std::vector<double>  myLastTicks;                // absolute-time tick positions

	// ---- RT state: beat phase ----
	// The time base MUST be double: one garbage getTimeInfo()->rate cook
	// (dt = 1/rate) can jump a float accumulator past its absorption point,
	// freezing it and collapsing every tick onto it bit-exactly.
	float  myBeatPhase       = 0.0f;
	double myAccumulatedTime = 0.0;    // seconds since first frame

	// ---- RT output values ----
	float myOutOnset          = 0.0f;
	float myOutOnsetStrength  = 0.0f;
	float myOutBpm            = 0.0f;
	float myOutBeat           = 0.0f;
	float myOutBeatPhase      = 0.0f;
	float myOutBeatConfidence = 0.0f;

	// ---- Batch-specific state ----
	float myDetectedBpm  = 0.0f;
	bool  myUsedAutocorr = false;
};

} // namespace EssentiaTD
