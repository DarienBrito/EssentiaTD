#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Shared/UnifiedCHOPBase.h"
#include "Parameters_Loudness.h"
#include "Shared/RingBuffer.h"

#include <essentia/algorithmfactory.h>
#include <vector>
#include <string>
#include <deque>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Worker parameter snapshot — captured at async launch time
// ---------------------------------------------------------------------------

struct BatchLoudnessParams
{
	int   frameSize    = 1024;
	float gateThreshDb = -70.0f;
	float zcrThreshold = 0.0f;
};

// ---------------------------------------------------------------------------
// EssentiaLoudnessCHOP
//
// Unified real-time + batch loudness analyser.
//
// Outputs 7 channels (fixed):
//   0  loudness            — per-frame Essentia perceived loudness (dBFS-like)
//   1  loudness_momentary  — EBU R128 momentary   (400 ms power-average)
//   2  loudness_shortterm  — EBU R128 short-term  (3 s power-average)
//   3  loudness_integrated — EBU R128 integrated  (gated running average)
//   4  dynamic_range       — short-term peak-to-valley swing
//   5  rms                 — frame RMS amplitude
//   6  zcr                 — zero-crossing rate
//
// Mode parameter selects:
//   Realtime — time-sliced, processes incoming audio every cook
//   Batch    — async offline analysis of a static audio buffer
// ---------------------------------------------------------------------------

class EssentiaLoudnessCHOP : public UnifiedCHOPBase<EssentiaLoudnessCHOP>
{
	friend class UnifiedCHOPBase<EssentiaLoudnessCHOP>;

public:
	explicit EssentiaLoudnessCHOP(const TD::OP_NodeInfo* info)
		: UnifiedCHOPBase<EssentiaLoudnessCHOP>(info)
	{}

	~EssentiaLoudnessCHOP() override
	{
		releaseAlgorithms();
	}

	static constexpr int kNumChannels = 7;

	// ----- Static async worker (no `this` pointer) -----

	static AsyncBatchResult computeBatchAsync(
		const AudioSnapshot&       audio,
		const BatchLoudnessParams& params,
		const std::atomic<bool>&   cancelFlag,
		std::atomic<float>&        progress);

	// ----- CRTP hooks -----

	bool getOutputInfoImpl(TD::CHOP_OutputInfo* info,
	                       const TD::OP_Inputs* inputs,
	                       bool isBatch);

	void getChannelNameImpl(int32_t index,
	                        TD::OP_String* name,
	                        const TD::OP_Inputs* inputs);

	void executeRealtimeImpl(TD::CHOP_Output* output,
	                         const TD::OP_Inputs* inputs);

	void snapshotAndLaunch(AudioSnapshot audio,
	                       const TD::OP_Inputs* inputs);

	void setupParametersImpl(TD::OP_ParameterManager* manager);

	int32_t getNumInfoCHOPChansImpl();
	void    getInfoCHOPChanImpl(int32_t index, TD::OP_InfoCHOPChan* chan);

	// ----- Base overrides -----

	/// Write cached batch results applying live normalization.
	void writeOutputBatch(TD::CHOP_Output* output, const TD::OP_Inputs* inputs);

	/// Loudness uses timeslice in realtime mode.
	bool isTimesliceInRealtime() { return true; }

private:
	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	void configureAlgorithms(int frameSize, float zcrThreshold);
	void releaseAlgorithms();
	void processFrame();
	static float windowedLoudnessDb(const std::deque<float>& window);
	void recomputeIntegrated(float gateThreshDb);

	// -------------------------------------------------------------------------
	// Real-time configuration state
	// -------------------------------------------------------------------------
	int    myFrameSize    = 0;
	double mySampleRate   = 0.0;
	float  myZcrThreshold = -1.0f; // force initial configure

	// -------------------------------------------------------------------------
	// Audio accumulation (real-time)
	// -------------------------------------------------------------------------
	RingBuffer myAudioRing;
	int        myHopCounter = 0;

	std::vector<essentia::Real> myAudioFrame;

	// -------------------------------------------------------------------------
	// Essentia algorithms (owned, real-time)
	// -------------------------------------------------------------------------
	essentia::standard::Algorithm* myLoudnessAlgo = nullptr;
	essentia::standard::Algorithm* myZcrAlgo      = nullptr;

	essentia::Real myEssentiaLoudness = 0.0f;
	essentia::Real myEssentiaZcr      = 0.0f;

	// -------------------------------------------------------------------------
	// Real-time loudness state (all values in dB)
	// -------------------------------------------------------------------------
	float myLoudnessDb           = -100.0f;

	std::deque<float> myMomentaryWindow;
	int               myMomentaryCapacity = 1;

	std::deque<float> myShortTermWindow;
	int               myShortTermCapacity = 1;

	std::vector<float> myIntegratedValues;

	// -------------------------------------------------------------------------
	// Latest real-time output values
	// -------------------------------------------------------------------------
	float myMomentaryLoudness  = -100.0f;
	float myShortTermLoudness  = -100.0f;
	float myIntegratedLoudness = -100.0f;
	float myDynamicRange       = 0.0f;
	float myRms                = 0.0f;
	float myZcr                = 0.0f;
	float myLoudnessDbMin      = 0.0f;
	float myLoudnessDbMax      = -144.0f;
};

} // namespace EssentiaTD
