// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Rhythm.h"
#include "Shared/BatchCommon.h"

#include <cstdlib>
#include <cstring>

using namespace TD;

namespace EssentiaTD
{

void ParametersRhythm::setup(OP_ParameterManager* manager)
{
	// Mode parameter (page "Mode")
	setupModeParam(manager);

	// Compute / Autocompute (page "Batch")
	setupBatchParams(manager);

	// FFT parameters (page "Analysis") — Windowtype is shared with RT mode;
	// FFT size / hop / zero-padding are batch-only, hidden in RT mode
	// Rhythm defaults: FFT 2048, hop 512 (matches Essentia Onsets default frameRate)
	setupBatchFftParams(manager, "Analysis", "2048", 512);

	// RT analysis window — RT runs its own FFT on the latest window each cook.
	// No "auto" entry: resolveAutoFftSize encodes Tonal's semitone bound, which
	// does not apply to onset detection.
	{
		OP_StringParameter p;
		p.name         = RtwindowsizeName;
		p.label        = RtwindowsizeLabel;
		p.page         = "Analysis";
		p.defaultValue = "1024";

		const char* names[]  = { "512", "1024", "2048", "4096" };
		const char* labels[] = { "512", "1024", "2048", "4096" };
		manager->appendMenu(p, 4, names, labels);
	}

	// ---- Rhythm page ----

	// Rhythm Method — batch-only: menu multifeature / degara
	{
		OP_StringParameter p;
		p.name         = RhythmmethodName;
		p.label        = RhythmmethodLabel;
		p.page         = "Rhythm";
		p.defaultValue = "degara";

		const char* names[]  = { "multifeature", "degara" };
		const char* labels[] = { "Multi-Feature", "Degara" };
		manager->appendMenu(p, 2, names, labels);
	}

	// Onset Method — menu: hfc / complex / flux / melflux / rms
	{
		OP_StringParameter p;
		p.name         = OnsetmethodName;
		p.label        = OnsetmethodLabel;
		p.page         = "Rhythm";
		p.defaultValue = "complex";

		const char* names[]  = { "hfc",  "complex",  "flux",  "melflux",  "rms",  "superflux"  };
		const char* labels[] = { "HFC",  "Complex",  "Flux",  "Mel Flux", "RMS",  "SuperFlux"  };
		manager->appendMenu(p, 6, names, labels);
	}

	// Onset Sensitivity — float [0.0, 1.0], default 0.5
	{
		OP_NumericParameter p;
		p.name             = OnsetsensitivityName;
		p.label            = OnsetsensitivityLabel;
		p.page             = "Rhythm";
		p.defaultValues[0] = 0.5;
		p.minSliders[0]    = 0.0;
		p.maxSliders[0]    = 1.0;
		p.minValues[0]     = 0.0;
		p.maxValues[0]     = 1.0;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendFloat(p);
	}

	// BPM Min — int [40, 180], default 60  (matches TempoTapDegara / RhythmExtractor2013 valid minTempo)
	{
		OP_NumericParameter p;
		p.name             = BpmminName;
		p.label            = BpmminLabel;
		p.page             = "Rhythm";
		p.defaultValues[0] = 60;
		p.minSliders[0]    = 40;
		p.maxSliders[0]    = 180;
		p.minValues[0]     = 40;
		p.maxValues[0]     = 180;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendInt(p);
	}

	// BPM Max — int [60, 250], default 180  (matches TempoTapDegara / RhythmExtractor2013 valid maxTempo)
	{
		OP_NumericParameter p;
		p.name             = BpmmaxName;
		p.label            = BpmmaxLabel;
		p.page             = "Rhythm";
		p.defaultValues[0] = 180;
		p.minSliders[0]    = 60;
		p.maxSliders[0]    = 250;
		p.minValues[0]     = 60;
		p.maxValues[0]     = 250;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
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
	if (std::strcmp(val, "melflux") == 0) return 3;
	if (std::strcmp(val, "rms")     == 0) return 4;
	if (std::strcmp(val, "superflux") == 0) return 5;
	return 0; // hfc
}

float ParametersRhythm::evalOnsetsensitivity(const OP_Inputs* inputs)
{
	return static_cast<float>(inputs->getParDouble(OnsetsensitivityName));
}

int ParametersRhythm::evalBpmmin(const OP_Inputs* inputs)
{
	return inputs->getParInt(BpmminName);
}

int ParametersRhythm::evalBpmmax(const OP_Inputs* inputs)
{
	return inputs->getParInt(BpmmaxName);
}

int ParametersRhythm::evalRhythmmethod(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(RhythmmethodName);
	if (val && std::strcmp(val, "degara") == 0) return 1;
	return 0; // multifeature
}

int ParametersRhythm::evalRtwindowsize(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(RtwindowsizeName);
	if (!val || val[0] == '\0') return 1024;
	const int v = std::atoi(val);
	return (v > 0) ? v : 1024;
}

} // namespace EssentiaTD
