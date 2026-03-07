#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Loudness.h"
#include "Shared/RingBuffer.h"

#include <essentia/algorithmfactory.h>
#include <vector>
#include <string>
#include <deque>

namespace EssentiaTD
{

/// EssentiaLoudnessCHOP
///
/// Time-sliced CHOP that accepts raw audio (mono, first channel used) and
/// outputs five loudness descriptors per cook:
///
///   loudness            — Essentia perceived loudness for the current frame (dBFS-like)
///   loudness_momentary  — EBU R128 momentary loudness (400 ms power-average window)
///   loudness_shortterm  — EBU R128 short-term loudness (3 s power-average window)
///   loudness_integrated — EBU R128 integrated loudness (gated running average)
///   dynamic_range       — peak-to-valley swing of short-term loudness over 3 s
///
/// EBU R128 windowing is implemented internally; Essentia's `Loudness`
/// algorithm supplies the per-frame power estimate that feeds each window.
///
/// Parameters (page "Loudness"):
///   Framesize      — menu: 512 / 1024 / 2048 (default 1024)
///   Gatethreshold  — float, -80 to -20 dB, default -70 (absolute gate for
///                    integrated loudness, relative gate is fixed at -10 dB)
class EssentiaLoudnessCHOP : public TD::CHOP_CPlusPlusBase
{
public:
	EssentiaLoudnessCHOP(const TD::OP_NodeInfo* info);
	~EssentiaLoudnessCHOP() override;

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
	void getErrorString(TD::OP_String* error, void* reserved1)   override;

	// Number of output channels (fixed)
	static constexpr int kNumChannels = 7;

private:
	// -------------------------------------------------------------------------
	// Internal helpers
	// -------------------------------------------------------------------------

	/// Create / recreate Essentia algorithms when parameters change.
	void configureAlgorithms(int frameSize, float zcrThreshold);

	/// Release all owned Essentia algorithm pointers.
	void releaseAlgorithms();

	/// Process one accumulated audio frame through the loudness pipeline.
	/// Updates myLoudnessDb, and pushes a new value into the window deques.
	void processFrame();

	/// Compute the power-averaged loudness (in dB) of the values in `window`.
	/// Values in `window` are already stored as dB; this converts to power,
	/// averages, then converts back.  Returns -144 dB when window is empty.
	static float windowedLoudnessDb(const std::deque<float>& window);

	/// Recompute myIntegratedLoudness from myIntegratedValues using a two-pass
	/// absolute + relative gate.
	void recomputeIntegrated(float gateThreshDb);

	// -------------------------------------------------------------------------
	// Configuration state
	// -------------------------------------------------------------------------
	int    myFrameSize     = 0;
	double mySampleRate   = 0.0;
	float  myZcrThreshold = -1.0f; // force initial configure

	// -------------------------------------------------------------------------
	// Audio accumulation
	// -------------------------------------------------------------------------
	RingBuffer myAudioRing;       // holds >= frameSize raw samples
	int        myHopCounter = 0;  // samples written since last frame dispatch

	// Pre-allocated frame buffer fed to Essentia
	std::vector<essentia::Real> myAudioFrame;

	// -------------------------------------------------------------------------
	// Essentia algorithms (owned)
	// -------------------------------------------------------------------------
	essentia::standard::Algorithm* myLoudnessAlgo = nullptr;
	essentia::standard::Algorithm* myZcrAlgo      = nullptr;

	// Output binding targets for Essentia algorithms
	essentia::Real myEssentiaLoudness = 0.0f;
	essentia::Real myEssentiaZcr      = 0.0f;

	// -------------------------------------------------------------------------
	// Loudness state (all values in dB, stored as float)
	// -------------------------------------------------------------------------

	/// Per-frame loudness (dBFS-like): 10 * log10(L + 1e-10)
	float myLoudnessDb = -100.0f;

	/// Momentary window: holds per-frame dB values spanning ~400 ms.
	/// Max capacity = ceil(0.4 * sampleRate / frameSize) frames.
	std::deque<float> myMomentaryWindow;
	int               myMomentaryCapacity = 1;

	/// Short-term window: holds per-frame dB values spanning ~3 s.
	/// Max capacity = ceil(3.0 * sampleRate / frameSize) frames.
	std::deque<float> myShortTermWindow;
	int               myShortTermCapacity = 1;

	/// All short-term loudness values ever computed — used for integrated gate.
	/// Cleared on reconfigure.
	std::vector<float> myIntegratedValues;

	// -------------------------------------------------------------------------
	// Latest output values (written by processFrame, read by execute)
	// -------------------------------------------------------------------------
	float myMomentaryLoudness  = -100.0f;
	float myShortTermLoudness  = -100.0f;
	float myIntegratedLoudness = -100.0f;
	float myDynamicRange       = 0.0f;
	float myRms                = 0.0f;
	float myZcr                = 0.0f;
	float myLoudnessDbMin      = 0.0f;   // running min of raw loudness dB
	float myLoudnessDbMax      = -144.0f; // running max of raw loudness dB

	// -------------------------------------------------------------------------
	// Init state
	// -------------------------------------------------------------------------
	bool myInitOk = false;

	// -------------------------------------------------------------------------
	// Diagnostics
	// -------------------------------------------------------------------------
	std::string myError;
	std::string myWarning;
};

} // namespace EssentiaTD
