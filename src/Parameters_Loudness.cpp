// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Loudness.h"

#include <cstdlib>

using namespace TD;

namespace EssentiaTD
{

void ParametersLoudness::setup(OP_ParameterManager* manager)
{
	// Frame Size — menu: 512, 1024 (default), 2048
	{
		OP_StringParameter p;
		p.name         = FramesizeName;
		p.label        = FramesizeLabel;
		p.page         = "Loudness";
		p.defaultValue = "1024";

		const char* names[]  = { "512",  "1024", "2048" };
		const char* labels[] = { "512",  "1024", "2048" };
		manager->appendMenu(p, 3, names, labels);
	}

	// Gate Threshold — float, -80 to -20 dB, default -70
	{
		OP_NumericParameter p;
		p.name             = GatethresholdName;
		p.label            = GatethresholdLabel;
		p.page             = "Loudness";
		p.defaultValues[0] = -70.0;
		p.minSliders[0]    = -80.0;
		p.maxSliders[0]    = -20.0;
		p.minValues[0]     = -80.0;
		p.maxValues[0]     = -20.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// Normalize — toggle, default off
	{
		OP_NumericParameter p;
		p.name             = NormalizeName;
		p.label            = NormalizeLabel;
		p.page             = "Loudness";
		p.defaultValues[0] = 0.0;
		manager->appendToggle(p);
	}

	// dB Floor — float, -100 to -10, default -60
	{
		OP_NumericParameter p;
		p.name             = DbfloorName;
		p.label            = DbfloorLabel;
		p.page             = "Loudness";
		p.defaultValues[0] = -60.0;
		p.minSliders[0]    = -100.0;
		p.maxSliders[0]    = -10.0;
		p.minValues[0]     = -100.0;
		p.maxValues[0]     = -10.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}
}

// ---------------------------------------------------------------------------
// Evaluators
// ---------------------------------------------------------------------------

int ParametersLoudness::evalFramesize(const OP_Inputs* inputs)
{
	return std::atoi(inputs->getParString(FramesizeName));
}

float ParametersLoudness::evalGatethreshold(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(GatethresholdName);
}

bool ParametersLoudness::evalNormalize(const OP_Inputs* inputs)
{
	return inputs->getParInt(NormalizeName) != 0;
}

float ParametersLoudness::evalDbfloor(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(DbfloorName);
}

} // namespace EssentiaTD
