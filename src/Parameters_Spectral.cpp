// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Spectral.h"
#include "Shared/BatchCommon.h"

using namespace TD;

namespace EssentiaTD
{

void ParametersSpectral::setup(OP_ParameterManager* manager)
{
	// --- Mode parameter (page "Mode") ---
	setupModeParam(manager);

	// --- Batch trigger params (page "Batch") ---
	setupBatchParams(manager);

	// --- FFT params (page "Spectrum") ---
	setupBatchFftParams(manager);

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
		p.defaultValues[0] = 1;
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

	// --- PCA group ---

	// Enable PCA toggle
	{
		OP_NumericParameter p;
		p.name           = EnablepcaName;
		p.label          = EnablepcaLabel;
		p.page           = "PCA";
		p.defaultValues[0] = 0;
		manager->appendToggle(p);
	}

	// PCA Components (int, 2-16, default 3)
	{
		OP_NumericParameter p;
		p.name           = PcacomponentsName;
		p.label          = PcacomponentsLabel;
		p.page           = "PCA";
		p.defaultValues[0] = 3;
		p.minSliders[0]  = 2;
		p.maxSliders[0]  = 16;
		p.minValues[0]   = 2;
		p.maxValues[0]   = 16;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendInt(p);
	}

	// PCA Window Size menu (128/256/512/1024/2048/4096, default 512) — RT only
	{
		OP_StringParameter p;
		p.name           = PcawindowsizeName;
		p.label          = PcawindowsizeLabel;
		p.page           = "PCA";
		p.defaultValue   = "512";

		const char* names[]  = { "128", "256", "512", "1024", "2048", "4096" };
		const char* labels[] = { "128", "256", "512", "1024", "2048", "4096" };
		manager->appendMenu(p, 6, names, labels);
	}

	// PCA Update Rate (int, 1-60, default 1 = once per second) — RT only
	{
		OP_NumericParameter p;
		p.name           = PcaupdaterateName;
		p.label          = PcaupdaterateLabel;
		p.page           = "PCA";
		p.defaultValues[0] = 1;
		p.minSliders[0]  = 1;
		p.maxSliders[0]  = 60;
		p.minValues[0]   = 1;
		p.maxValues[0]   = 60;
		p.clampMins[0]   = true;
		p.clampMaxes[0]  = true;
		manager->appendInt(p);
	}

	// PCA Variance Channels toggle
	{
		OP_NumericParameter p;
		p.name           = PcavarianceName;
		p.label          = PcavarianceLabel;
		p.page           = "PCA";
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

// PCA evaluators

bool ParametersSpectral::evalEnablepca(const OP_Inputs* inputs)
{
	return inputs->getParInt(EnablepcaName) != 0;
}

int ParametersSpectral::evalPcacomponents(const OP_Inputs* inputs)
{
	return inputs->getParInt(PcacomponentsName);
}

int ParametersSpectral::evalPcawindowsize(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(PcawindowsizeName);
	if (!val || val[0] == '\0') return 512;
	int v = std::atoi(val);
	return (v > 0) ? v : 512;
}

int ParametersSpectral::evalPcaupdaterate(const OP_Inputs* inputs)
{
	return inputs->getParInt(PcaupdaterateName);
}

bool ParametersSpectral::evalPcavariance(const OP_Inputs* inputs)
{
	return inputs->getParInt(PcavarianceName) != 0;
}

} // namespace EssentiaTD
