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

constexpr static char SmoothingName[]   = "Smoothing";
constexpr static char SmoothingLabel[]  = "Smoothing";

constexpr static char KeyframesName[]   = "Keyframes";
constexpr static char KeyframesLabel[]  = "Key Frames";

constexpr static char KeyprofileName[]  = "Keyprofile";
constexpr static char KeyprofileLabel[] = "Key Profile";

constexpr static char PitchminfreqName[]  = "Pitchminfreq";
constexpr static char PitchminfreqLabel[] = "Pitch Min Freq";

constexpr static char PitchmaxfreqName[]  = "Pitchmaxfreq";
constexpr static char PitchmaxfreqLabel[] = "Pitch Max Freq";

constexpr static char PitchtoleranceName[]  = "Pitchtolerance";
constexpr static char PitchtoleranceLabel[] = "Pitch Tolerance";

constexpr static char PeakthresholdName[]  = "Peakthreshold";
constexpr static char PeakthresholdLabel[] = "Peak Threshold";

constexpr static char PeakmaxfreqName[]    = "Peakmaxfreq";
constexpr static char PeakmaxfreqLabel[]   = "Peak Max Freq";

constexpr static char HpcpharmonicsName[]  = "Hpcpharmonics";
constexpr static char HpcpharmonicsLabel[] = "HPCP Harmonics";

constexpr static char ReferencefreqName[]  = "Referencefreq";
constexpr static char ReferencefreqLabel[] = "Reference Freq";

constexpr static char HpcpnonlinearName[]  = "Hpcpnonlinear";
constexpr static char HpcpnonlinearLabel[] = "HPCP Non-Linear";

constexpr static char HpcpnormalizedName[]  = "Hpcpnormalized";
constexpr static char HpcpnormalizedLabel[] = "HPCP Normalized";

// ---------------------------------------------------------------------------

class ParametersTonal
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	/// Returns 0 = yinfft, 1 = yinprobabilistic
	static int   evalPitchalgo(const TD::OP_Inputs* inputs);
	static int   evalHpcpsize(const TD::OP_Inputs* inputs);
	static bool  evalEnablepitch(const TD::OP_Inputs* inputs);
	static bool  evalEnablehpcp(const TD::OP_Inputs* inputs);
	static bool  evalEnablekey(const TD::OP_Inputs* inputs);
	static bool  evalEnabledissonance(const TD::OP_Inputs* inputs);
	static bool  evalEnableinharmonicity(const TD::OP_Inputs* inputs);
	static bool  evalMusicallabels(const TD::OP_Inputs* inputs);
	static bool  evalEnablepitchnote(const TD::OP_Inputs* inputs);
	static float evalSmoothing(const TD::OP_Inputs* inputs);
	static int   evalKeyframes(const TD::OP_Inputs* inputs);

	// New algorithm tuning parameters
	static int   evalKeyprofile(const TD::OP_Inputs* inputs);
	static float evalPitchminfreq(const TD::OP_Inputs* inputs);
	static float evalPitchmaxfreq(const TD::OP_Inputs* inputs);
	static float evalPitchtolerance(const TD::OP_Inputs* inputs);
	static float evalPeakthreshold(const TD::OP_Inputs* inputs);
	static float evalPeakmaxfreq(const TD::OP_Inputs* inputs);
	static int   evalHpcpharmonics(const TD::OP_Inputs* inputs);
	static float evalReferencefreq(const TD::OP_Inputs* inputs);
	static bool  evalHpcpnonlinear(const TD::OP_Inputs* inputs);
	static int   evalHpcpnormalized(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
