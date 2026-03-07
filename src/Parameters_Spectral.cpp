// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Spectral.h"

using namespace TD;

namespace EssentiaTD
{

void ParametersSpectral::setup(OP_ParameterManager* manager)
{
	// --- MFCC group ---

	// Enable MFCC toggle
	{
		OP_NumericParameter p;
		p.name           = EnablemfccName;
		p.label          = EnablemfccLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// MFCC coefficient count (int, 1-20, default 13)
	{
		OP_NumericParameter p;
		p.name           = MffcccountName;
		p.label          = MffcccountLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 13;
		p.minSliders[0]  = 1;
		p.maxSliders[0]  = 20;
		p.minValues[0]   = 1;
		p.maxValues[0]   = 20;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendInt(p);
	}

	// MFCC Low Frequency Bound
	{
		OP_NumericParameter p;
		p.name           = MfcclowfreqName;
		p.label          = MfcclowfreqLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0.0;
		p.minSliders[0]  = 0.0;
		p.maxSliders[0]  = 8000.0;
		p.minValues[0]   = 0.0;
		p.maxValues[0]   = 22050.0;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// MFCC High Frequency Bound
	{
		OP_NumericParameter p;
		p.name           = MfcchighfreqName;
		p.label          = MfcchighfreqLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 11000.0;
		p.minSliders[0]  = 1000.0;
		p.maxSliders[0]  = 22050.0;
		p.minValues[0]   = 1000.0;
		p.maxValues[0]   = 22050.0;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// --- Spectral features ---

	// Enable Centroid toggle
	{
		OP_NumericParameter p;
		p.name           = EnablecentroidName;
		p.label          = EnablecentroidLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Flux toggle
	{
		OP_NumericParameter p;
		p.name           = EnablefluxName;
		p.label          = EnablefluxLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Flux Half Rectify toggle
	{
		OP_NumericParameter p;
		p.name           = FluxhalfrectifyName;
		p.label          = FluxhalfrectifyLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Flux Norm menu (L1 / L2)
	{
		OP_StringParameter p;
		p.name           = FluxnormName;
		p.label          = FluxnormLabel;
		p.page           = "Spectral";
		p.defaultValue   = "L2";

		const char* names[]  = { "L1", "L2" };
		const char* labels[] = { "L1", "L2" };
		manager->appendMenu(p, 2, names, labels);
	}

	// Enable Rolloff toggle
	{
		OP_NumericParameter p;
		p.name           = EnablerolloffName;
		p.label          = EnablerolloffLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Rolloff Cutoff — float [0.5, 0.99], default 0.85
	{
		OP_NumericParameter p;
		p.name           = RolloffcutoffName;
		p.label          = RolloffcutoffLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0.85;
		p.minSliders[0]  = 0.5;
		p.maxSliders[0]  = 0.99;
		p.minValues[0]   = 0.5;
		p.maxValues[0]   = 0.99;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// Enable Spectral Contrast toggle
	{
		OP_NumericParameter p;
		p.name           = EnablecontrastName;
		p.label          = EnablecontrastLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Contrast Bands menu (4 / 6 / 8)
	{
		OP_StringParameter p;
		p.name           = ContrastbandsName;
		p.label          = ContrastbandsLabel;
		p.page           = "Spectral";
		p.defaultValue   = "6";

		const char* names[]  = { "4", "6", "8" };
		const char* labels[] = { "4", "6", "8" };
		manager->appendMenu(p, 3, names, labels);
	}

	// Enable HFC toggle
	{
		OP_NumericParameter p;
		p.name           = EnablehfcName;
		p.label          = EnablehfcLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// HFC Type menu (Masri / Jensen / Brossier)
	{
		OP_StringParameter p;
		p.name           = HfctypeName;
		p.label          = HfctypeLabel;
		p.page           = "Spectral";
		p.defaultValue   = "Masri";

		const char* names[]  = { "Masri", "Jensen", "Brossier" };
		const char* labels[] = { "Masri", "Jensen", "Brossier" };
		manager->appendMenu(p, 3, names, labels);
	}

	// Enable Complexity toggle
	{
		OP_NumericParameter p;
		p.name           = EnablecomplexityName;
		p.label          = EnablecomplexityLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Complexity Magnitude Threshold — float [0, 0.1], default 0.005
	{
		OP_NumericParameter p;
		p.name           = ComplexitythreshName;
		p.label          = ComplexitythreshLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0.005;
		p.minSliders[0]  = 0.0;
		p.maxSliders[0]  = 0.1;
		p.minValues[0]   = 0.0;
		p.maxValues[0]   = 0.1;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// --- Mel Bands group ---

	// Enable Mel Bands toggle
	{
		OP_NumericParameter p;
		p.name           = EnablemelName;
		p.label          = EnablemelLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Mel Bands count menu (24/40/60/80/128, default 40)
	{
		OP_StringParameter p;
		p.name           = MelbandscountName;
		p.label          = MelbandscountLabel;
		p.page           = "Spectral";
		p.defaultValue   = "40";

		const char* names[]  = { "24", "40", "60", "80", "128" };
		const char* labels[] = { "24", "40", "60", "80", "128" };
		manager->appendMenu(p, 5, names, labels);
	}

	// Mel Low Frequency Bound
	{
		OP_NumericParameter p;
		p.name           = MellowfreqName;
		p.label          = MellowfreqLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0.0;
		p.minSliders[0]  = 0.0;
		p.maxSliders[0]  = 8000.0;
		p.minValues[0]   = 0.0;
		p.maxValues[0]   = 22050.0;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// Mel High Frequency Bound
	{
		OP_NumericParameter p;
		p.name           = MelhighfreqName;
		p.label          = MelhighfreqLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 22050.0;
		p.minSliders[0]  = 1000.0;
		p.maxSliders[0]  = 22050.0;
		p.minValues[0]   = 1000.0;
		p.maxValues[0]   = 22050.0;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendFloat(p);
	}

	// Mel Freq Names toggle
	{
		OP_NumericParameter p;
		p.name           = MelfreqnamesName;
		p.label          = MelfreqnamesLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// Log Mel toggle (dB conversion)
	{
		OP_NumericParameter p;
		p.name           = MellogName;
		p.label          = MellogLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

}

// ---------------------------------------------------------------------------
// Evaluators
// ---------------------------------------------------------------------------

bool ParametersSpectral::evalEnablemfcc(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablemfccName) != 0;
}

int ParametersSpectral::evalMfcccount(const OP_Inputs* inputs)
{
	return inputs->getParInt(MffcccountName);
}

bool ParametersSpectral::evalEnablecentroid(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablecentroidName) != 0;
}

bool ParametersSpectral::evalEnableflux(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablefluxName) != 0;
}

bool ParametersSpectral::evalEnablerolloff(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablerolloffName) != 0;
}

bool ParametersSpectral::evalEnablecontrast(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablecontrastName) != 0;
}

bool ParametersSpectral::evalEnablehfc(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablehfcName) != 0;
}

bool ParametersSpectral::evalEnablecomplexity(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablecomplexityName) != 0;
}

bool ParametersSpectral::evalEnablemel(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablemelName) != 0;
}

int ParametersSpectral::evalMelbandscount(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(MelbandscountName);
	if (!val || val[0] == '\0') return 40;
	int v = std::atoi(val);
	return (v > 0) ? v : 40;
}

bool ParametersSpectral::evalMelfreqnames(const OP_Inputs* inputs)
{
	return inputs->getParInt(MelfreqnamesName) != 0;
}

bool ParametersSpectral::evalMellog(const OP_Inputs* inputs)
{
	return inputs->getParInt(MellogName) != 0;
}

float ParametersSpectral::evalMfcclowfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(MfcclowfreqName);
}

float ParametersSpectral::evalMfcchighfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(MfcchighfreqName);
}

float ParametersSpectral::evalRolloffcutoff(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(RolloffcutoffName);
}

int ParametersSpectral::evalHfctype(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(HfctypeName);
	if (!val || val[0] == '\0') return 0;
	if (std::strcmp(val, "Jensen") == 0)   return 1;
	if (std::strcmp(val, "Brossier") == 0) return 2;
	return 0; // Masri
}

bool ParametersSpectral::evalFluxhalfrectify(const OP_Inputs* inputs)
{
	return inputs->getParInt(FluxhalfrectifyName) != 0;
}

int ParametersSpectral::evalFluxnorm(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(FluxnormName);
	if (!val || val[0] == '\0') return 1;
	if (std::strcmp(val, "L1") == 0) return 0;
	return 1; // L2
}

float ParametersSpectral::evalComplexitythresh(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(ComplexitythreshName);
}

int ParametersSpectral::evalContrastbands(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(ContrastbandsName);
	if (!val || val[0] == '\0') return 6;
	int v = std::atoi(val);
	return (v > 0) ? v : 6;
}

float ParametersSpectral::evalMellowfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(MellowfreqName);
}

float ParametersSpectral::evalMelhighfreq(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(MelhighfreqName);
}

} // namespace EssentiaTD
