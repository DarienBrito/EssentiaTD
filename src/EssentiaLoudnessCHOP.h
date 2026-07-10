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
// ITU-R BS.1770-4 K-weighting biquad filter (Transposed Direct Form II)
// ---------------------------------------------------------------------------

struct Biquad
{
	double b0 = 0, b1 = 0, b2 = 0;
	double a1 = 0, a2 = 0;  // a0 normalized to 1
	double z1 = 0, z2 = 0;  // filter state

	void reset() { z1 = z2 = 0; }

	float process(float x)
	{
		const double xd = static_cast<double>(x);
		const double y  = b0 * xd + z1;
		z1 = b1 * xd - a1 * y + z2;
		z2 = b2 * xd - a2 * y;
		return static_cast<float>(y);
	}
};

/// Compute stage-1 (high-shelf) coefficients for K-weighting.
void computeKWeightShelf(double sampleRate, Biquad& bq);

/// Compute stage-2 (high-pass / RLB) coefficients for K-weighting.
void computeKWeightHighpass(double sampleRate, Biquad& bq);

// ---------------------------------------------------------------------------
// Worker parameter snapshot — captured at async launch time
// ---------------------------------------------------------------------------

struct BatchLoudnessParams
{
	int   frameSize    = 1024;
	float zcrThreshold = 0.0f;
};

// ---------------------------------------------------------------------------
// EssentiaLoudnessCHOP
//
// Unified real-time + batch loudness analyser using ITU-R BS.1770
// K-weighted filtering for proper LUFS output.
//
// Outputs 7 channels (fixed):
//   0  loudness            — per-frame K-weighted LUFS (instantaneous)
//   1  loudness_momentary  — EBU R128 momentary   (400 ms power-average, LUFS)
//   2  loudness_shortterm  — EBU R128 short-term  (3 s power-average, LUFS)
//   3  loudness_integrated — EBU R128 integrated  (gated, -70 LUFS abs, LUFS)
//   4  dynamic_range       — short-term peak-to-valley swing (LU)
//   5  rms                 — frame RMS amplitude (linear)
//   6  zcr                 — zero-crossing rate (0-1)
//
// Mode parameter selects:
//   Realtime — processes incoming audio every cook
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

private:
	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	void configureAlgorithms(int frameSize, double sampleRate, float zcrThreshold);
	void releaseAlgorithms();
	void processFrame();
	static float windowedLufs(const std::deque<float>& window);
	void recomputeIntegrated();

	// -------------------------------------------------------------------------
	// Real-time configuration state
	// -------------------------------------------------------------------------
	int    myFrameSize    = 0;
	double mySampleRate   = 0.0;
	float  myZcrThreshold = -1.0f; // force initial configure

	// -------------------------------------------------------------------------
	// K-weighting filters (real-time, persistent state between frames)
	// -------------------------------------------------------------------------
	Biquad myKWeightStage1;
	Biquad myKWeightStage2;

	// -------------------------------------------------------------------------
	// Audio accumulation (real-time)
	// -------------------------------------------------------------------------
	RingBuffer myAudioRing;
	int        myHopCounter = 0;

	std::vector<essentia::Real> myAudioFrame;

	// -------------------------------------------------------------------------
	// Essentia algorithms (owned, real-time) — ZCR only
	// -------------------------------------------------------------------------
	essentia::standard::Algorithm* myZcrAlgo = nullptr;
	essentia::Real myEssentiaZcr = 0.0f;

	// -------------------------------------------------------------------------
	// Real-time loudness state (LUFS)
	// -------------------------------------------------------------------------
	float myLoudnessLufs = -144.0f;

	std::deque<float> myMomentaryWindow;
	int               myMomentaryCapacity = 1;

	std::deque<float> myShortTermWindow;
	int               myShortTermCapacity = 1;

	std::vector<float> myIntegratedValues;

	// -------------------------------------------------------------------------
	// Latest real-time output values
	// -------------------------------------------------------------------------
	float myMomentaryLoudness  = -144.0f;
	float myShortTermLoudness  = -144.0f;
	float myIntegratedLoudness = -144.0f;
	float myDynamicRange       = 0.0f;
	float myRms                = 0.0f;
	float myZcr                = 0.0f;
	float myLufsMin            = 0.0f;
	float myLufsMax            = -144.0f;
};

} // namespace EssentiaTD
