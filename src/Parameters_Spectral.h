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

constexpr static char MelfreqnamesName[]     = "Melfreqnames";
constexpr static char MelfreqnamesLabel[]    = "Mel Freq Names";

constexpr static char MellogName[]           = "Mellog";
constexpr static char MellogLabel[]          = "Log Mel (dB Scale)";

// Algorithm tuning parameters
constexpr static char MfcclowfreqName[]      = "Mfcclowfreq";
constexpr static char MfcclowfreqLabel[]     = "MFCC Low Freq";

constexpr static char MfcchighfreqName[]     = "Mfcchighfreq";
constexpr static char MfcchighfreqLabel[]    = "MFCC High Freq";

constexpr static char RolloffcutoffName[]    = "Rolloffcutoff";
constexpr static char RolloffcutoffLabel[]   = "Rolloff Cutoff";

constexpr static char HfctypeName[]          = "Hfctype";
constexpr static char HfctypeLabel[]         = "HFC Type";

constexpr static char FluxhalfrectifyName[]  = "Fluxhalfrectify";
constexpr static char FluxhalfrectifyLabel[] = "Flux Half Rectify";

constexpr static char FluxnormName[]         = "Fluxnorm";
constexpr static char FluxnormLabel[]        = "Flux Norm";

constexpr static char ComplexitythreshName[]  = "Complexitythresh";
constexpr static char ComplexitythreshLabel[] = "Complexity Threshold";

constexpr static char ContrastbandsName[]    = "Contrastbands";
constexpr static char ContrastbandsLabel[]   = "Contrast Bands";

constexpr static char MellowfreqName[]       = "Mellowfreq";
constexpr static char MellowfreqLabel[]      = "Mel Low Freq";

constexpr static char MelhighfreqName[]      = "Melhighfreq";
constexpr static char MelhighfreqLabel[]     = "Mel High Freq";


// ---------------------------------------------------------------------------
// ParametersSpectral
// ---------------------------------------------------------------------------

class ParametersSpectral
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	// Evaluators
	static bool  evalEnablemfcc(const TD::OP_Inputs* inputs);
	static int   evalMfcccount(const TD::OP_Inputs* inputs);
	static float evalMfcclowfreq(const TD::OP_Inputs* inputs);
	static float evalMfcchighfreq(const TD::OP_Inputs* inputs);
	static bool  evalEnablecentroid(const TD::OP_Inputs* inputs);
	static bool  evalEnableflux(const TD::OP_Inputs* inputs);
	static bool  evalEnablerolloff(const TD::OP_Inputs* inputs);
	static float evalRolloffcutoff(const TD::OP_Inputs* inputs);
	static bool  evalEnablecontrast(const TD::OP_Inputs* inputs);
	static int   evalContrastbands(const TD::OP_Inputs* inputs);
	static bool  evalEnablehfc(const TD::OP_Inputs* inputs);
	static int   evalHfctype(const TD::OP_Inputs* inputs);
	static bool  evalEnablecomplexity(const TD::OP_Inputs* inputs);
	static float evalComplexitythresh(const TD::OP_Inputs* inputs);
	static bool  evalFluxhalfrectify(const TD::OP_Inputs* inputs);
	static int   evalFluxnorm(const TD::OP_Inputs* inputs);
	static bool  evalEnablemel(const TD::OP_Inputs* inputs);
	static int   evalMelbandscount(const TD::OP_Inputs* inputs);
	static float evalMellowfreq(const TD::OP_Inputs* inputs);
	static float evalMelhighfreq(const TD::OP_Inputs* inputs);
	static bool  evalMelfreqnames(const TD::OP_Inputs* inputs);
	static bool  evalMellog(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
