#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unified Rhythm CHOP — real-time onset/BPM analysis (Mode=Realtime)
// or full-file batch rhythm extraction (Mode=Batch).
//
// Real-time path:
//   Analyzes raw audio — runs its own FFT via RTFrameProcessor (latest
//   Window Size samples per cook), then OnsetDetection (real phase from the
//   internal CartesianToPolar) or SuperFluxNovelty for onset strength,
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
//
// With Enable Drums on, 6 more (both modes):
//   kick / snare / hat                   : hit trigger (0 or 1)
//   kick_level / snare_level / hat_level : band energy, 0-1 of its running peak

#include "Shared/UnifiedCHOPBase.h"
#include "Shared/DrumDetector.h"
#include "Shared/RTFrameProcessor.h"
#include "Shared/Utils.h"
#include "Parameters_Rhythm.h"

#include <essentia/algorithmfactory.h>

#include <deque>
#include <string>
#include <tuple>
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
	static constexpr int kNumDrumChannels   = 2 * DrumDetector::kNumClasses;

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
		bool        drums;
		int         drumWindow;
		float       kickSens;
		float       snareSens;
		float       hatSens;

		// Every field, so a cached result can tell when the node's parameters
		// moved past it. A field missing here is a parameter the staleness
		// warning cannot see.
		auto tie() const
		{
			return std::tie(fftSize, hopSize, windowType, zeroPad,
			                rhythmMethod, onsetMethodIdx, sensitivity,
			                bpmMin, bpmMax,
			                drums, drumWindow, kickSens, snareSens, hatSens);
		}
		bool operator==(const BatchRhythmParams& o) const { return tie() == o.tie(); }
		bool operator!=(const BatchRhythmParams& o) const { return !(*this == o); }
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

	/// Single source of truth for the batch parameter snapshot: the launch
	/// and the staleness check must read the same values or the warning lies.
	static BatchRhythmParams evalBatchParams(const TD::OP_Inputs* inputs);
	bool    batchParamsStaleImpl(const TD::OP_Inputs* inputs);
	BatchRhythmParams myLaunchedParams;
	bool              myHasLaunchedParams = false;

	void    setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	void    onResultCollected(AsyncBatchResult& result);

	// v2.0: reject legacy spectrum wiring with a migration error (both modes)
	const char* inputContractErrorImpl(const TD::OP_CHOPInput* in)
	{
		return spectrumInputContractError(in);
	}

	// ---- RT: algorithm lifecycle ----

	/// (Re-)create onset algorithms for the given spectrum size and method.
	/// Index 0-4: OnsetDetection (hfc/complex/flux/melflux/rms)
	/// Index 5:   SuperFlux (TriangularBands + SuperFluxNovelty)
	void configureOnsetDetection(int specSize, int onsetMethodIdx, double sampleRate);
	void releaseAlgorithms();

	// ---- RT: helpers ----

	/// dt = seconds of audio the cook carried, stored alongside the value so
	/// the tempo stage can measure the history's true duration.
	void pushOnsetStrength(float value, double dt);

	/// Onsets-style adaptive onset detection.
	bool detectOnset(float odfValue, float sensitivity, double frameRate);

	/// Run TempoTapDegara periodically on accumulated ODF buffer.
	/// getTimeInfo()->rate guarded against transient garbage values —
	/// one absurd rate poisons dt and every time-derived state after it.
	static double sanitizedFrameRate(const TD::OP_Inputs* inputs);

	void updateTempoEstimate(double frameRate, int bpmMin, int bpmMax);

	/// Advance beat phase, anchored to TempoTapDegara ticks when available.
	void updateBeatPhase(double dt, int bpmMin, int bpmMax);

	void writeOutputs(TD::CHOP_Output* output);
	void writeDrumChannels(TD::CHOP_Output* output);

	/// Drum detector on its own FFT (Drum Window). Runs every cook the input
	/// cooked, independent of the onset pipeline's early returns.
	void processDrums(const TD::OP_CHOPInput* chopIn, double sampleRate,
	                  const TD::OP_Inputs* inputs);

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
	std::vector<std::vector<essentia::Real>> myBandsMatrix;      // SuperFluxNovelty input (reused per cook)

	// ---- RT: internal FFT (v2.0 — input is raw audio in both modes) ----
	RTFrameProcessor myFrameProc;
	int              myRtFftSize = 0;   // window myFrameProc is built for
	std::string      myRtWindowType;

	// Configuration tracking (onset algorithms)
	int    mySpecSize          = 0;     // spectrum bins = window/2+1
	double mySampleRate        = 0.0;   // audio input rate
	int    myCurrentMethodIdx  = -1;

	// ---- RT state: onset history circular buffer (feeds TempoTapDegara) ----
	static constexpr int kOnsetHistorySize = 512;
	std::vector<float>   myOnsetHistory;
	std::vector<float>   myOnsetDt;        ///< seconds of audio per history slot
	int                  myOnsetWritePos  = 0;
	int                  myOnsetFillCount = 0;

	// ---- RT state: Onsets-style adaptive threshold ----
	// Time-based (converted to a per-cook frame count / decay factor from the
	// sanitized frame rate) so onset behavior is frame-rate independent.
	static constexpr float kThresholdDelaySeconds = 5.0f / 60.0f; // ~83 ms lookback (was 5 frames @ 60 fps)
	static constexpr float kDecayTimeConstant     = 0.158f;       // s; reproduces 0.9/frame decay @ 60 fps
	// Silence gate is RELATIVE to the signal's own running ODF peak — an
	// absolute gate silently disables onset detection for methods whose ODF
	// scale sits below it (and never engages for high-scale methods)
	static constexpr float kSilenceRelative  = 0.01f;  // fraction of running peak
	static constexpr float kSilenceFloor     = 1e-6f;  // true digital silence
	float myOdfRunningPeak = 0.0f;
	std::deque<float> myOdfLookback;                   // recent ODF values; window sized per frame rate
	float myDecayThreshold  = 0.0f;
	bool  myFrameSilent     = false;   ///< last detectOnset() took the silence branch

	// ---- RT state: TempoTapDegara BPM estimation ----
	static constexpr float kTempoUpdateSeconds = 1.5f; // interval in s (frames = seconds * frame rate)
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

	// ---- RT state: drums ----
	RTFrameProcessor myDrumFrameProc;
	DrumDetector     myDrums;
	int              myDrumWindow     = 0;    // 0 = not configured
	double           myDrumSampleRate = 0.0;
	float            myOutDrums[kNumDrumChannels] = {};

	// ---- Batch-specific state ----
	float myDetectedBpm  = 0.0f;
	bool  myUsedAutocorr = false;
};

} // namespace EssentiaTD
