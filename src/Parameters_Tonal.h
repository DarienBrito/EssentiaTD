#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// --- Parameter name constants ---

constexpr static char PitchalgoName[]  = "Pitchalgo";
constexpr static char PitchalgoLabel[] = "Pitch Algorithm";

constexpr static char HpcpsizeName[]   = "Hpcpsize";
constexpr static char HpcpsizeLabel[]  = "HPCP Size";

constexpr static char EnablepitchName[]   = "Enablepitch";
constexpr static char EnablepitchLabel[]  = "Enable Pitch";

constexpr static char EnablehpcpName[]   = "Enablehpcp";
constexpr static char EnablehpcpLabel[]  = "Enable HPCP";

constexpr static char EnablekeyName[]   = "Enablekey";
constexpr static char EnablekeyLabel[]  = "Enable Key";

constexpr static char EnabledissonanceName[]   = "Enabledissonance";
constexpr static char EnabledissonanceLabel[]  = "Enable Dissonance";

constexpr static char EnableinharmonicityName[]   = "Enableinharmonicity";
constexpr static char EnableinharmonicityLabel[]  = "Enable Inharmonicity";

constexpr static char MusicallabelsName[]   = "Musicallabels";
constexpr static char MusicallabelsLabel[]  = "Musical Labels";

constexpr static char EnablepitchnoteName[]   = "Enablepitchnote";
constexpr static char EnablepitchnoteLabel[]  = "Enable Pitch Note";

// ---------------------------------------------------------------------------

class ParametersTonal
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	// Evaluators — each reads a single parameter from the OP_Inputs handle.
	// Menu parameters return the selected item string; the eval methods parse
	// them into the caller-friendly types shown below.

	/// Returns 0 = yinfft, 1 = yinprobabilistic
	static int  evalPitchalgo(const TD::OP_Inputs* inputs);

	/// Returns the integer HPCP bin count: 12, 24, or 36
	static int  evalHpcpsize(const TD::OP_Inputs* inputs);

	static bool evalEnablepitch(const TD::OP_Inputs* inputs);
	static bool evalEnablehpcp(const TD::OP_Inputs* inputs);
	static bool evalEnablekey(const TD::OP_Inputs* inputs);
	static bool evalEnabledissonance(const TD::OP_Inputs* inputs);
	static bool evalEnableinharmonicity(const TD::OP_Inputs* inputs);
	static bool evalMusicallabels(const TD::OP_Inputs* inputs);
	static bool evalEnablepitchnote(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
