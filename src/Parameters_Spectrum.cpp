// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Parameters_Spectrum.h"

using namespace TD;

namespace EssentiaTD
{

void ParametersSpectrum::setup(OP_ParameterManager* manager)
{
	// FFT Size — menu: 512, 1024, 2048, 4096
	{
		OP_StringParameter p;
		p.name = SpecFftsizeName;
		p.label = SpecFftsizeLabel;
		p.page = "Spectrum";
		p.defaultValue = "1024";

		const char* names[] = { "512", "1024", "2048", "4096", "8192", "16384" };
		const char* labels[] = { "512", "1024", "2048", "4096", "8192", "16384" };
		manager->appendMenu(p, 6, names, labels);
	}

	// Hop Size
	{
		OP_NumericParameter p;
		p.name = SpecHopsizeName;
		p.label = SpecHopsizeLabel;
		p.page = "Spectrum";
		p.defaultValues[0] = 512;
		p.minSliders[0] = 64;
		p.maxSliders[0] = 16384;
		p.minValues[0] = 64;
		p.maxValues[0] = 16384;
		p.clampMins[0] = true;
		p.clampMaxes[0] = true;
		manager->appendInt(p);
	}

	// Window Type — menu: hann, hamming, triangular, blackmanharris62/70/74/92
	{
		OP_StringParameter p;
		p.name = SpecWindowtypeName;
		p.label = SpecWindowtypeLabel;
		p.page = "Spectrum";
		p.defaultValue = "hann";

		const char* names[] = {
			"hann", "hamming", "triangular",
			"blackmanharris62", "blackmanharris70",
			"blackmanharris74", "blackmanharris92"
		};
		const char* labels[] = {
			"Hann", "Hamming", "Triangular",
			"Blackman-Harris 62", "Blackman-Harris 70",
			"Blackman-Harris 74", "Blackman-Harris 92"
		};
		manager->appendMenu(p, 7, names, labels);
	}

	// Zero Padding — menu: 0, fftSize/2, fftSize
	{
		OP_StringParameter p;
		p.name = SpecZeropaddingName;
		p.label = SpecZeropaddingLabel;
		p.page = "Spectrum";
		p.defaultValue = "0";

		const char* names[] = { "0", "1", "2" };
		const char* labels[] = { "None", "Half FFT", "Full FFT" };
		manager->appendMenu(p, 3, names, labels);
	}
}

int ParametersSpectrum::evalFftsize(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(SpecFftsizeName);
	if (!val || val[0] == '\0') return 1024;
	int v = std::atoi(val);
	return (v > 0) ? v : 1024;
}

int ParametersSpectrum::evalHopsize(const OP_Inputs* inputs)
{
	int v = inputs->getParInt(SpecHopsizeName);
	return (v > 0) ? v : 512;
}

int ParametersSpectrum::evalWindowtype(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(SpecWindowtypeName);
	if (!val || val[0] == '\0') return 0;
	// Returns the raw string index; the CHOP maps it to an Essentia name
	if (std::strcmp(val, "hamming") == 0)            return 1;
	if (std::strcmp(val, "triangular") == 0)         return 2;
	if (std::strcmp(val, "blackmanharris62") == 0)    return 3;
	if (std::strcmp(val, "blackmanharris70") == 0)    return 4;
	if (std::strcmp(val, "blackmanharris74") == 0)    return 5;
	if (std::strcmp(val, "blackmanharris92") == 0)    return 6;
	return 0; // hann
}

int ParametersSpectrum::evalZeropadding(const OP_Inputs* inputs)
{
	const char* val = inputs->getParString(SpecZeropaddingName);
	if (!val || val[0] == '\0') return 0;
	return std::atoi(val);
}

} // namespace EssentiaTD
