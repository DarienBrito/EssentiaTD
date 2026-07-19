#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Parameter name / label constants  (RT + Batch combined)
// ---------------------------------------------------------------------------

constexpr static char OnsetmethodName[]       = "Onsetmethod";
constexpr static char OnsetmethodLabel[]      = "Onset Method";

constexpr static char OnsetsensitivityName[]  = "Onsetsensitivity";
constexpr static char OnsetsensitivityLabel[] = "Onset Sensitivity";

constexpr static char BpmminName[]            = "Bpmmin";
constexpr static char BpmminLabel[]           = "BPM Min";

constexpr static char BpmmaxName[]            = "Bpmmax";
constexpr static char BpmmaxLabel[]           = "BPM Max";

// Batch-only
constexpr static char RhythmmethodName[]      = "Rhythmmethod";
constexpr static char RhythmmethodLabel[]     = "Rhythm Method";

// RT-only
constexpr static char RtwindowsizeName[]      = "Rtwindowsize";
constexpr static char RtwindowsizeLabel[]     = "Window Size";

// ---------------------------------------------------------------------------
// ParametersRhythm
// ---------------------------------------------------------------------------

class ParametersRhythm
{
public:
	/// Append all rhythm page parameters to the manager.
	/// Called after setupModeParam / setupBatchParams / setupBatchFftParams.
	static void setup(TD::OP_ParameterManager* manager);

	/// Returns 0 = hfc, 1 = complex, 2 = flux, 3 = melflux, 4 = rms, 5 = superflux
	static int   evalOnsetmethod(const TD::OP_Inputs* inputs);

	/// Returns 0.0 – 1.0
	static float evalOnsetsensitivity(const TD::OP_Inputs* inputs);

	/// Returns integer minimum BPM [40, 180]
	static int   evalBpmmin(const TD::OP_Inputs* inputs);

	/// Returns integer maximum BPM [60, 250]
	static int   evalBpmmax(const TD::OP_Inputs* inputs);

	/// Returns 0 = multifeature, 1 = degara (batch-only)
	static int   evalRhythmmethod(const TD::OP_Inputs* inputs);

	/// Returns RT analysis window in samples (512/1024/2048/4096), default 1024.
	/// RT-only; no "auto" — onset detection has no semitone-resolution bound.
	static int   evalRtwindowsize(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
