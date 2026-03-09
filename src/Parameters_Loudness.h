#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Parameter name / label constants
// ---------------------------------------------------------------------------

constexpr static char FramesizeName[]      = "Framesize";
constexpr static char FramesizeLabel[]     = "Frame Size";

constexpr static char ZcrthresholdName[]   = "Zcrthreshold";
constexpr static char ZcrthresholdLabel[]  = "ZCR Threshold";

// ---------------------------------------------------------------------------
// ParametersLoudness
// ---------------------------------------------------------------------------

class ParametersLoudness
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	/// Returns the integer frame size: 512, 1024, or 2048.
	static int   evalFramesize(const TD::OP_Inputs* inputs);

	/// Returns the ZCR dead-band threshold (0 = count every sign change).
	static float evalZcrthreshold(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
