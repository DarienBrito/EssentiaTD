#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Parameter name / label constants
// ---------------------------------------------------------------------------

constexpr static char EnablemfccName[]       = "Enablemfcc";
constexpr static char EnablemfccLabel[]      = "Enable MFCC";

constexpr static char MffcccountName[]       = "Mfcccount";
constexpr static char MffcccountLabel[]      = "MFCC Count";

constexpr static char EnablecentroidName[]   = "Enablecentroid";
constexpr static char EnablecentroidLabel[]  = "Enable Centroid";

constexpr static char EnablefluxName[]       = "Enableflux";
constexpr static char EnablefluxLabel[]      = "Enable Flux";

constexpr static char EnablerolloffName[]    = "Enablerolloff";
constexpr static char EnablerolloffLabel[]   = "Enable Rolloff";

constexpr static char EnablecontrastName[]   = "Enablecontrast";
constexpr static char EnablecontrastLabel[]  = "Enable Contrast";

constexpr static char EnablehfcName[]        = "Enablehfc";
constexpr static char EnablehfcLabel[]       = "Enable HFC";

constexpr static char EnablecomplexityName[] = "Enablecomplexity";
constexpr static char EnablecomplexityLabel[]= "Enable Complexity";

constexpr static char EnablemelName[]        = "Enablemel";
constexpr static char EnablemelLabel[]       = "Enable Mel Bands";

constexpr static char MelbandscountName[]    = "Melbandscount";
constexpr static char MelbandscountLabel[]   = "Mel Bands Count";

// ---------------------------------------------------------------------------
// ParametersSpectral
// ---------------------------------------------------------------------------

class ParametersSpectral
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	// Evaluators
	static bool evalEnablemfcc(const TD::OP_Inputs* inputs);
	static int  evalMfcccount(const TD::OP_Inputs* inputs);
	static bool evalEnablecentroid(const TD::OP_Inputs* inputs);
	static bool evalEnableflux(const TD::OP_Inputs* inputs);
	static bool evalEnablerolloff(const TD::OP_Inputs* inputs);
	static bool evalEnablecontrast(const TD::OP_Inputs* inputs);
	static bool evalEnablehfc(const TD::OP_Inputs* inputs);
	static bool evalEnablecomplexity(const TD::OP_Inputs* inputs);
	static bool evalEnablemel(const TD::OP_Inputs* inputs);
	static int  evalMelbandscount(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
