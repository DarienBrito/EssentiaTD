// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Tonal.h"

#include <cstring>
#include <cstdlib>

using namespace TD;

namespace EssentiaTD
{

void ParametersTonal::setup(OP_ParameterManager* manager)
{
	// Pitch Algorithm — menu: yinfft (default), yinprobabilistic
	{
		OP_StringParameter p;
		p.name         = PitchalgoName;
		p.label        = PitchalgoLabel;
		p.page         = "Tonal";
		p.defaultValue = "yinfft";

		const char* names[]  = { "yinfft",   "yinprobabilistic"   };
		const char* labels[] = { "YinFFT",   "YinProbabilistic"   };
		manager->appendMenu(p, 2, names, labels);
	}

	// HPCP Size — menu: 12 (default), 24, 36
	{
		OP_StringParameter p;
		p.name         = HpcpsizeName;
		p.label        = HpcpsizeLabel;
		p.page         = "Tonal";
		p.defaultValue = "12";

		const char* names[]  = { "12", "24", "36" };
		const char* labels[] = { "12", "24", "36" };
		manager->appendMenu(p, 3, names, labels);
	}

	// Enable Pitch toggle (on by default)
	{
		OP_NumericParameter p;
		p.name             = EnablepitchName;
		p.label            = EnablepitchLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable HPCP toggle (on by default)
	{
		OP_NumericParameter p;
		p.name             = EnablehpcpName;
		p.label            = EnablehpcpLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Key toggle (on by default)
	{
		OP_NumericParameter p;
		p.name             = EnablekeyName;
		p.label            = EnablekeyLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Dissonance toggle (on by default)
	{
		OP_NumericParameter p;
		p.name             = EnabledissonanceName;
		p.label            = EnabledissonanceLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Inharmonicity toggle (on by default)
	{
		OP_NumericParameter p;
		p.name             = EnableinharmonicityName;
		p.label            = EnableinharmonicityLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Musical Labels toggle (off by default)
	{
		OP_NumericParameter p;
		p.name             = MusicallabelsName;
		p.label            = MusicallabelsLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Enable Pitch Note toggle (off by default)
	{
		OP_NumericParameter p;
		p.name             = EnablepitchnoteName;
		p.label            = EnablepitchnoteLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Smoothing — float [0.0, 1.0], default 0.5
	{
		OP_NumericParameter p;
		p.name             = SmoothingName;
		p.label            = SmoothingLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0.5;
		p.minSliders[0]    = 0.0;
		p.maxSliders[0]    = 1.0;
		p.minValues[0]     = 0.0;
		p.maxValues[0]     = 1.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Key Frames — int [1, 300], default 8
	{
		OP_NumericParameter p;
		p.name             = KeyframesName;
		p.label            = KeyframesLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 8;
		p.minSliders[0]    = 1;
		p.maxSliders[0]    = 300;
		p.minValues[0]     = 1;
		p.maxValues[0]     = 300;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendInt(p);
	}
}

// ---------------------------------------------------------------------------
// Evaluators
// ---------------------------------------------------------------------------

int ParametersTonal::evalPitchalgo(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(PitchalgoName);
	if (std::strcmp(val, "yinprobabilistic") == 0) return 1;
	return 0; // yinfft (default)
}

int ParametersTonal::evalHpcpsize(const OP_Inputs* inputs)
{
	return std::atoi(inputs->getParString(HpcpsizeName));
}

bool ParametersTonal::evalEnablepitch(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablepitchName) != 0;
}

bool ParametersTonal::evalEnablehpcp(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablehpcpName) != 0;
}

bool ParametersTonal::evalEnablekey(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablekeyName) != 0;
}

bool ParametersTonal::evalEnabledissonance(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnabledissonanceName) != 0;
}

bool ParametersTonal::evalEnableinharmonicity(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnableinharmonicityName) != 0;
}

bool ParametersTonal::evalMusicallabels(const OP_Inputs* inputs)
{
	return inputs->getParInt(MusicallabelsName) != 0;
}

bool ParametersTonal::evalEnablepitchnote(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablepitchnoteName) != 0;
}

float ParametersTonal::evalSmoothing(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(SmoothingName);
}

int ParametersTonal::evalKeyframes(const OP_Inputs* inputs)
{
	return inputs->getParInt(KeyframesName);
}

} // namespace EssentiaTD
