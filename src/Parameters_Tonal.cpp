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

	// Key Profile — menu
	{
		OP_StringParameter p;
		p.name         = KeyprofileName;
		p.label        = KeyprofileLabel;
		p.page         = "Tonal";
		p.defaultValue = "bgate";

		const char* names[]  = { "bgate", "temperley", "krumhansl", "edma", "diatonic", "gomez" };
		const char* labels[] = { "Bgate", "Temperley", "Krumhansl", "EDMA", "Diatonic", "Gomez" };
		manager->appendMenu(p, 6, names, labels);
	}

	// Pitch Min Freq — float [20, 2000], default 20
	{
		OP_NumericParameter p;
		p.name             = PitchminfreqName;
		p.label            = PitchminfreqLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 20.0;
		p.minSliders[0]    = 20.0;
		p.maxSliders[0]    = 2000.0;
		p.minValues[0]     = 20.0;
		p.maxValues[0]     = 2000.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Pitch Max Freq — float [200, 22050], default 22050
	{
		OP_NumericParameter p;
		p.name             = PitchmaxfreqName;
		p.label            = PitchmaxfreqLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 22050.0;
		p.minSliders[0]    = 200.0;
		p.maxSliders[0]    = 22050.0;
		p.minValues[0]     = 200.0;
		p.maxValues[0]     = 22050.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Pitch Tolerance — float [0, 1], default 1.0
	{
		OP_NumericParameter p;
		p.name             = PitchtoleranceName;
		p.label            = PitchtoleranceLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 1.0;
		p.minSliders[0]    = 0.0;
		p.maxSliders[0]    = 1.0;
		p.minValues[0]     = 0.0;
		p.maxValues[0]     = 1.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Peak Magnitude Threshold — float [0, 0.01], default 0
	{
		OP_NumericParameter p;
		p.name             = PeakthresholdName;
		p.label            = PeakthresholdLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0.0;
		p.minSliders[0]    = 0.0;
		p.maxSliders[0]    = 0.01;
		p.minValues[0]     = 0.0;
		p.maxValues[0]     = 1.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Peak Max Frequency — float [1000, 22050], default 5000
	{
		OP_NumericParameter p;
		p.name             = PeakmaxfreqName;
		p.label            = PeakmaxfreqLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 5000.0;
		p.minSliders[0]    = 1000.0;
		p.maxSliders[0]    = 22050.0;
		p.minValues[0]     = 1000.0;
		p.maxValues[0]     = 22050.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// HPCP Harmonics — int [0, 12], default 0
	{
		OP_NumericParameter p;
		p.name             = HpcpharmonicsName;
		p.label            = HpcpharmonicsLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0;
		p.minSliders[0]    = 0;
		p.maxSliders[0]    = 12;
		p.minValues[0]     = 0;
		p.maxValues[0]     = 12;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendInt(p);
	}

	// Reference Frequency — float [415, 466], default 440
	{
		OP_NumericParameter p;
		p.name             = ReferencefreqName;
		p.label            = ReferencefreqLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 440.0;
		p.minSliders[0]    = 415.0;
		p.maxSliders[0]    = 466.0;
		p.minValues[0]     = 415.0;
		p.maxValues[0]     = 466.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// HPCP Non-Linear toggle (off by default)
	{
		OP_NumericParameter p;
		p.name             = HpcpnonlinearName;
		p.label            = HpcpnonlinearLabel;
		p.page             = "Tonal";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// HPCP Normalized — menu: unitMax / unitSum / none
	{
		OP_StringParameter p;
		p.name         = HpcpnormalizedName;
		p.label        = HpcpnormalizedLabel;
		p.page         = "Tonal";
		p.defaultValue = "unitMax";

		const char* names[]  = { "unitMax", "unitSum", "none" };
		const char* labels[] = { "Unit Max", "Unit Sum", "None" };
		manager->appendMenu(p, 3, names, labels);
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

int ParametersTonal::evalKeyprofile(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(KeyprofileName);
	if (!val || val[0] == '\0') return 0;
	if (std::strcmp(val, "temperley") == 0)  return 1;
	if (std::strcmp(val, "krumhansl") == 0)  return 2;
	if (std::strcmp(val, "edma") == 0)       return 3;
	if (std::strcmp(val, "diatonic") == 0)   return 4;
	if (std::strcmp(val, "gomez") == 0)      return 5;
	return 0; // bgate
}

float ParametersTonal::evalPitchminfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(PitchminfreqName);
}

float ParametersTonal::evalPitchmaxfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(PitchmaxfreqName);
}

float ParametersTonal::evalPitchtolerance(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(PitchtoleranceName);
}

float ParametersTonal::evalPeakthreshold(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(PeakthresholdName);
}

float ParametersTonal::evalPeakmaxfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(PeakmaxfreqName);
}

int ParametersTonal::evalHpcpharmonics(const OP_Inputs* inputs)
{
	return inputs->getParInt(HpcpharmonicsName);
}

float ParametersTonal::evalReferencefreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(ReferencefreqName);
}

bool ParametersTonal::evalHpcpnonlinear(const OP_Inputs* inputs)
{
	return inputs->getParInt(HpcpnonlinearName) != 0;
}

int ParametersTonal::evalHpcpnormalized(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(HpcpnormalizedName);
	if (!val || val[0] == '\0') return 0;
	if (std::strcmp(val, "unitSum") == 0)  return 1;
	if (std::strcmp(val, "none") == 0)     return 2;
	return 0; // unitMax
}

} // namespace EssentiaTD
