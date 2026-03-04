// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Rhythm.h"

#include <cstring>

using namespace TD;

namespace EssentiaTD
{

void ParametersRhythm::setup(OP_ParameterManager* manager)
{
	// Onset Method — menu: hfc / complex / flux
	{
		OP_StringParameter p;
		p.name         = OnsetmethodName;
		p.label        = OnsetmethodLabel;
		p.page         = "Rhythm";
		p.defaultValue = "hfc";

		const char* names[]  = { "hfc",  "complex",  "flux"  };
		const char* labels[] = { "HFC",  "Complex",  "Flux"  };
		manager->appendMenu(p, 3, names, labels);
	}

	// Onset Sensitivity — float [0.0, 1.0], default 0.5
	{
		OP_NumericParameter p;
		p.name              = OnsetsensitivityName;
		p.label             = OnsetsensitivityLabel;
		p.page              = "Rhythm";
		p.defaultValues[0]  = 0.5;
		p.minSliders[0]     = 0.0;
		p.maxSliders[0]     = 1.0;
		p.minValues[0]      = 0.0;
		p.maxValues[0]      = 1.0;
		p.clampMins[0]      = true;
		p.clampMaxes[0]     = true;
		manager->appendFloat(p);
	}

	// BPM Min — int [30, 200], default 60
	{
		OP_NumericParameter p;
		p.name              = BpmminName;
		p.label             = BpmminLabel;
		p.page              = "Rhythm";
		p.defaultValues[0]  = 60;
		p.minSliders[0]     = 30;
		p.maxSliders[0]     = 200;
		p.minValues[0]      = 30;
		p.maxValues[0]      = 200;
		p.clampMins[0]      = true;
		p.clampMaxes[0]     = true;
		manager->appendInt(p);
	}

	// BPM Max — int [60, 300], default 180
	{
		OP_NumericParameter p;
		p.name              = BpmmaxName;
		p.label             = BpmmaxLabel;
		p.page              = "Rhythm";
		p.defaultValues[0]  = 180;
		p.minSliders[0]     = 60;
		p.maxSliders[0]     = 300;
		p.minValues[0]      = 60;
		p.maxValues[0]      = 300;
		p.clampMins[0]      = true;
		p.clampMaxes[0]     = true;
		manager->appendInt(p);
	}
}

// ---------------------------------------------------------------------------
// Evaluators
// ---------------------------------------------------------------------------

int ParametersRhythm::evalOnsetmethod(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(OnsetmethodName);
	if (std::strcmp(val, "complex") == 0) return 1;
	if (std::strcmp(val, "flux")    == 0) return 2;
	return 0; // hfc
}

float ParametersRhythm::evalOnsetsensitivity(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(OnsetsensitivityName);
}

int ParametersRhythm::evalBpmmin(const OP_Inputs* inputs)
{
	return inputs->getParInt(BpmminName);
}

int ParametersRhythm::evalBpmmax(const OP_Inputs* inputs)
{
	return inputs->getParInt(BpmmaxName);
}

} // namespace EssentiaTD
