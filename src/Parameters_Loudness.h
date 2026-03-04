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

constexpr static char GatethresholdName[]  = "Gatethreshold";
constexpr static char GatethresholdLabel[] = "Gate Threshold";

constexpr static char NormalizeName[]      = "Normalize";
constexpr static char NormalizeLabel[]     = "Normalize";

constexpr static char DbfloorName[]        = "Dbfloor";
constexpr static char DbfloorLabel[]       = "dB Floor";

constexpr static char DbceilingName[]      = "Dbceiling";
constexpr static char DbceilingLabel[]     = "dB Ceiling";

// ---------------------------------------------------------------------------
// ParametersLoudness
// ---------------------------------------------------------------------------

class ParametersLoudness
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	/// Returns the integer frame size: 512, 1024, or 2048.
	static int   evalFramesize(const TD::OP_Inputs* inputs);

	/// Returns the gate threshold in dB (float, -80 to -20, default -70).
	static float evalGatethreshold(const TD::OP_Inputs* inputs);

	/// Returns true when output should be normalized to 0–1.
	static bool  evalNormalize(const TD::OP_Inputs* inputs);

	/// Returns the dB floor used for normalization mapping.
	static float evalDbfloor(const TD::OP_Inputs* inputs);

	/// Returns the dB ceiling used for normalization mapping.
	static float evalDbceiling(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
