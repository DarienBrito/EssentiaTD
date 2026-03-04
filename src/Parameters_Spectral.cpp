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
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Rolloff toggle
	{
		OP_NumericParameter p;
		p.name           = EnablerolloffName;
		p.label          = EnablerolloffLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}

	// Enable Spectral Contrast toggle
	{
		OP_NumericParameter p;
		p.name           = EnablecontrastName;
		p.label          = EnablecontrastLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
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

	// Enable Complexity toggle
	{
		OP_NumericParameter p;
		p.name           = EnablecomplexityName;
		p.label          = EnablecomplexityLabel;
		p.page           = "Spectral";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
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

} // namespace EssentiaTD
