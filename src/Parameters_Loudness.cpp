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

	// ZCR Threshold — float, 0 to 0.1, default 0
	{
		OP_NumericParameter p;
		p.name             = ZcrthresholdName;
		p.label            = ZcrthresholdLabel;
		p.page             = "Loudness";
		p.defaultValues[0] = 0.0;
		p.minSliders[0]    = 0.0;
		p.maxSliders[0]    = 0.1;
		p.minValues[0]     = 0.0;
		p.maxValues[0]     = 0.1;
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

float ParametersLoudness::evalZcrthreshold(const OP_Inputs* inputs)
{
	return (float)inputs->getParDouble(ZcrthresholdName);
}

} // namespace EssentiaTD
