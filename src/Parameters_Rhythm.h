#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Parameter name / label constants
// ---------------------------------------------------------------------------

constexpr static char OnsetmethodName[]       = "Onsetmethod";
constexpr static char OnsetmethodLabel[]      = "Onset Method";

constexpr static char OnsetsensitivityName[]  = "Onsetsensitivity";
constexpr static char OnsetsensitivityLabel[] = "Onset Sensitivity";

constexpr static char BpmminName[]            = "Bpmmin";
constexpr static char BpmminLabel[]           = "BPM Min";

constexpr static char BpmmaxName[]            = "Bpmmax";
constexpr static char BpmmaxLabel[]           = "BPM Max";

constexpr static char TempobiasName[]         = "Tempobias";
constexpr static char TempobiasLabel[]        = "Tempo Bias";

constexpr static char BiascenterName[]        = "Biascenter";
constexpr static char BiascenterLabel[]       = "Bias Center BPM";

// ---------------------------------------------------------------------------
// ParametersRhythm
// ---------------------------------------------------------------------------

class ParametersRhythm
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	/// Returns 0 = hfc, 1 = complex, 2 = flux, 3 = melflux, 4 = rms
	static int   evalOnsetmethod(const TD::OP_Inputs* inputs);

	/// Returns 0.0 – 1.0
	static float evalOnsetsensitivity(const TD::OP_Inputs* inputs);

	/// Returns integer minimum BPM [30, 200]
	static int   evalBpmmin(const TD::OP_Inputs* inputs);

	/// Returns integer maximum BPM [60, 300]
	static int   evalBpmmax(const TD::OP_Inputs* inputs);

	/// Returns true if tempo bias is enabled
	static bool  evalTempobias(const TD::OP_Inputs* inputs);

	/// Returns bias center BPM [30, 300]
	static float evalBiascenter(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
