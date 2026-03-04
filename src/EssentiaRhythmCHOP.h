#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Rhythm.h"

#include <essentia/algorithmfactory.h>

#include <array>
#include <string>
#include <vector>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// EssentiaRhythmCHOP
//
// Consumes a spectrum / mel-band CHOP produced by EssentiaCoreCHOP and
// outputs six rhythm / tempo channels (1 sample each):
//
//   onset           — onset trigger (0 or 1, resets to 0 next frame)
//   onset_strength  — raw onset detection function value
//   bpm             — tempo estimate in BPM
//   beat            — beat trigger (0 or 1, resets to 0 next frame)
//   beat_phase      — 0-1 position within the current beat cycle
//   beat_confidence — confidence of the BPM estimate (0-1)
//
// BPM estimation is fully real-time: autocorrelation over a circular buffer
// of recent onset-strength values, constrained to [bpmMin, bpmMax].
// ---------------------------------------------------------------------------

class EssentiaRhythmCHOP : public TD::CHOP_CPlusPlusBase
{
public:
	explicit EssentiaRhythmCHOP(const TD::OP_NodeInfo* info);
	~EssentiaRhythmCHOP() override;

	// ---- TD API ----
	void    getGeneralInfo(TD::CHOP_GeneralInfo* ginfo,
	                       const TD::OP_Inputs* inputs, void* reserved) override;

	bool    getOutputInfo(TD::CHOP_OutputInfo* info,
	                      const TD::OP_Inputs* inputs, void* reserved) override;

	void    getChannelName(int32_t index, TD::OP_String* name,
	                       const TD::OP_Inputs* inputs, void* reserved) override;

	void    execute(TD::CHOP_Output* output,
	                const TD::OP_Inputs* inputs, void* reserved) override;

	void    setupParameters(TD::OP_ParameterManager* manager,
	                        void* reserved) override;

	int32_t getNumInfoCHOPChans(void* reserved) override;
	void    getInfoCHOPChan(int32_t index,
	                        TD::OP_InfoCHOPChan* chan, void* reserved) override;

	void getWarningString(TD::OP_String* warning, void* reserved1) override;
	void getErrorString(TD::OP_String* error, void* reserved1)   override;

private:
	// ---- Algorithm lifecycle ----
	/// (Re-)create OnsetDetection for the given spectrum size and method.
	void configureOnsetDetection(int specSize, const char* method, double sampleRate);
	void releaseAlgorithms();

	// ---- Real-time BPM helpers ----
	/// Append one onset-strength sample to the circular history buffer.
	void pushOnsetStrength(float value);

	/// Compute autocorrelation-based BPM from myOnsetHistory.
	/// Uses harmonic summation and optional Gaussian tempo prior.
	/// Returns 0 if not enough data.
	float computeAutocorrBpm(int bpmMin, int bpmMax,
	                         double sampleRate, int hopSize,
	                         bool tempoBias, float biasCenter) const;

	/// Advance beat phase by one frame and fire beat trigger if phase wraps.
	/// Also clamps / commits BPM and confidence to the output members.
	void updateBeatPhase(int bpmMin, int bpmMax);

	/// Copy all myOut* members into the CHOP_Output channels.
	void writeOutputs(TD::CHOP_Output* output);

	// ---- State ----

	// Essentia
	essentia::standard::Algorithm* myOnsetDetection = nullptr;

	// Working buffers (pre-allocated)
	std::vector<essentia::Real> mySpectrumBuf;   ///< magnitude spectrum fed to OnsetDetection
	std::vector<essentia::Real> myPhaseBuf;      ///< zero-phase approximation (same size)
	essentia::Real              myOnsetValue = 0.0f; ///< output of OnsetDetection

	// Configuration tracking
	int         mySpecSize   = 0;
	double      mySampleRate = 0.0;
	int         myHopSize    = 0;       ///< estimated hop size in samples (derived from fps)
	std::string myCurrentMethod;

	// Onset history circular buffer for autocorrelation BPM
	// ~8 s at 44100 / 1024 hop ≈ 344 frames → use 512 slots for headroom
	static constexpr int kOnsetHistorySize = 512;
	std::vector<float>   myOnsetHistory;
	int                  myOnsetWritePos  = 0;
	int                  myOnsetFillCount = 0;   ///< how many slots contain real data

	// Real-time BPM state
	float mySmoothedBpm    = 120.0f; ///< exponential-average BPM output
	float myBeatPhase      = 0.0f;  ///< 0-1 within beat cycle
	mutable float myBeatConfidence = 0.0f;  ///< normalised autocorrelation peak

	// Median BPM filter (removes single-frame octave jumps before EMA)
	static constexpr int kBpmMedianSize = 5;
	std::array<float, kBpmMedianSize> myBpmMedianBuf = {};
	int myBpmMedianPos  = 0;
	int myBpmMedianFill = 0;

	// Adaptive onset normalization (per-instance, not shared)
	float myRunningMax = 1e-4f;

	// Output values (written each execute(), read by output channels)
	float myOutOnset          = 0.0f;
	float myOutOnsetStrength  = 0.0f;
	float myOutBpm            = 0.0f;
	float myOutBeat           = 0.0f;
	float myOutBeatPhase      = 0.0f;
	float myOutBeatConfidence = 0.0f;

	// Init state
	bool myInitOk = false;

	// Diagnostics / status strings
	std::string myError;
	std::string myWarning;

	// Frame counter used to derive a coarse fps estimate
	uint64_t myFrameCount  = 0;
	double   myFpsEstimate = 60.0; ///< updated each frame, used for beat phase increment
};

} // namespace EssentiaTD
