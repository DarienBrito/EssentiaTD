#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CPlusPlus_Common.h"

namespace EssentiaTD
{

// --- Parameter name constants ---
constexpr static char SpecFftsizeName[] = "Fftsize";
constexpr static char SpecFftsizeLabel[] = "FFT Size";

constexpr static char SpecHopsizeName[] = "Hopsize";
constexpr static char SpecHopsizeLabel[] = "Hop Size";

constexpr static char SpecWindowtypeName[] = "Windowtype";
constexpr static char SpecWindowtypeLabel[] = "Window Type";

class ParametersSpectrum
{
public:
	static void setup(TD::OP_ParameterManager* manager);

	// Evaluators
	static int evalFftsize(const TD::OP_Inputs* inputs);
	static int evalHopsize(const TD::OP_Inputs* inputs);
	static int evalWindowtype(const TD::OP_Inputs* inputs);
};

} // namespace EssentiaTD
