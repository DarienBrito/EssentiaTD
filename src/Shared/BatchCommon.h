#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared parameter names, evaluators, and utilities for batch analysis CHOPs.
// Header-only — included by each batch CHOP's parameter and implementation files.

#include "CPlusPlus_Common.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// Forward-declare TD types
namespace TD { class OP_CHOPInput; }

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Mode parameter (shared by all unified CHOPs)
// ---------------------------------------------------------------------------

constexpr static char ModeName[]  = "Mode";
constexpr static char ModeLabel[] = "Mode";

// ---------------------------------------------------------------------------
// Batch CHOP common parameter names
// ---------------------------------------------------------------------------

constexpr static char BatchComputeName[]      = "Compute";
constexpr static char BatchComputeLabel[]     = "Compute";

constexpr static char BatchAutocomputeName[]  = "Autocompute";
constexpr static char BatchAutocomputeLabel[] = "Auto Compute";

// ---------------------------------------------------------------------------
// FFT parameter names (reused by batch spectral, tonal, rhythm)
// ---------------------------------------------------------------------------

constexpr static char BatchFftsizeName[]      = "Fftsize";
constexpr static char BatchFftsizeLabel[]     = "FFT Size";

constexpr static char BatchHopsizeName[]      = "Hopsize";
constexpr static char BatchHopsizeLabel[]     = "Hop Size";

constexpr static char BatchWindowtypeName[]   = "Windowtype";
constexpr static char BatchWindowtypeLabel[]  = "Window Type";

constexpr static char BatchZeropaddingName[]  = "Zeropadding";
constexpr static char BatchZeropaddingLabel[] = "Zero Padding";

// ---------------------------------------------------------------------------
// Audio fingerprint — detects input changes for Autocompute
// ---------------------------------------------------------------------------

struct AudioFingerprint
{
	static constexpr int kNumProbes = 16;

	int    numSamples = 0;
	double sampleRate = 0.0;
	// Evenly strided probe samples (first + last included) — sampling only
	// the two boundary samples missed same-length content swaps whose ends
	// happened to match (e.g. clips faded to silence)
	std::array<float, kNumProbes> probes = {};

	bool operator==(const AudioFingerprint& o) const
	{
		return numSamples == o.numSamples && sampleRate == o.sampleRate
		    && probes == o.probes;
	}
	bool operator!=(const AudioFingerprint& o) const { return !(*this == o); }
};

inline AudioFingerprint makeFingerprint(const TD::OP_CHOPInput* in)
{
	AudioFingerprint fp;
	if (!in || in->numChannels < 1 || in->numSamples < 1) return fp;
	fp.numSamples  = in->numSamples;
	fp.sampleRate  = in->sampleRate;
	const float* d = in->getChannelData(0);
	const int64_t n = in->numSamples;
	for (int i = 0; i < AudioFingerprint::kNumProbes; ++i)
		fp.probes[static_cast<size_t>(i)] =
			d[(n - 1) * i / (AudioFingerprint::kNumProbes - 1)];
	return fp;
}

// ---------------------------------------------------------------------------
// Helper: append Mode parameter (page "Mode")
// ---------------------------------------------------------------------------

inline void setupModeParam(TD::OP_ParameterManager* manager)
{
	TD::OP_StringParameter p;
	p.name         = ModeName;
	p.label        = ModeLabel;
	p.page         = "Mode";
	p.defaultValue = "realtime";

	const char* names[]  = { "realtime", "batch" };
	const char* labels[] = { "Realtime", "Batch" };
	manager->appendMenu(p, 2, names, labels);
}

// ---------------------------------------------------------------------------
// Evaluator: returns 0 = realtime, 1 = batch
// ---------------------------------------------------------------------------

inline int evalMode(const TD::OP_Inputs* inputs)
{
	const char* val = inputs->getParString(ModeName);
	if (!val || val[0] == '\0') return 0;
	if (std::strcmp(val, "batch") == 0) return 1;
	return 0; // realtime
}

// ---------------------------------------------------------------------------
// Helper: append Compute + Autocompute parameters (page "Batch")
// ---------------------------------------------------------------------------

inline void setupBatchParams(TD::OP_ParameterManager* manager)
{
	{
		TD::OP_NumericParameter p;
		p.name  = BatchComputeName;
		p.label = BatchComputeLabel;
		p.page  = "Batch";
		manager->appendPulse(p);
	}
	{
		TD::OP_NumericParameter p;
		p.name             = BatchAutocomputeName;
		p.label            = BatchAutocomputeLabel;
		p.page             = "Batch";
		p.defaultValues[0] = 1;
		manager->appendToggle(p);
	}
}

// ---------------------------------------------------------------------------
// Helper: append FFT parameters (page configurable, default "Spectrum")
// ---------------------------------------------------------------------------

inline void setupBatchFftParams(TD::OP_ParameterManager* manager,
                                const char* page = "Spectrum",
                                const char* defaultFftSize = "2048",
                                int defaultHopSize = 1024)
{
	// FFT Size menu
	{
		TD::OP_StringParameter p;
		p.name         = BatchFftsizeName;
		p.label        = BatchFftsizeLabel;
		p.page         = page;
		p.defaultValue = defaultFftSize;

		const char* names[]  = { "512", "1024", "2048", "4096", "8192", "16384" };
		const char* labels[] = { "512", "1024", "2048", "4096", "8192", "16384" };
		manager->appendMenu(p, 6, names, labels);
	}

	// Hop Size
	{
		TD::OP_NumericParameter p;
		p.name             = BatchHopsizeName;
		p.label            = BatchHopsizeLabel;
		p.page             = page;
		p.defaultValues[0] = defaultHopSize;
		p.minSliders[0]    = 64;
		p.maxSliders[0]    = 16384;
		p.minValues[0]     = 64;
		p.maxValues[0]     = 16384;
		p.clampMins[0]     = true;
		p.clampMaxes[0]    = true;
		manager->appendInt(p);
	}

	// Window Type menu
	{
		TD::OP_StringParameter p;
		p.name         = BatchWindowtypeName;
		p.label        = BatchWindowtypeLabel;
		p.page         = page;
		p.defaultValue = "blackmanharris62";

		const char* names[]  = { "hann", "hamming", "triangular",
		    "blackmanharris62", "blackmanharris70",
		    "blackmanharris74", "blackmanharris92" };
		const char* labels[] = { "Hann", "Hamming", "Triangular",
		    "Blackman-Harris 62", "Blackman-Harris 70",
		    "Blackman-Harris 74", "Blackman-Harris 92" };
		manager->appendMenu(p, 7, names, labels);
	}

	// Zero Padding menu
	{
		TD::OP_StringParameter p;
		p.name         = BatchZeropaddingName;
		p.label        = BatchZeropaddingLabel;
		p.page         = page;
		p.defaultValue = "0";

		const char* names[]  = { "0", "1", "2" };
		const char* labels[] = { "None", "Half FFT", "Full FFT" };
		manager->appendMenu(p, 3, names, labels);
	}
}

// ---------------------------------------------------------------------------
// Evaluators for common batch + FFT parameters
// ---------------------------------------------------------------------------

inline bool evalBatchCompute(const TD::OP_Inputs* inputs)
{
	return inputs->getParInt(BatchComputeName) != 0;
}

inline bool evalBatchAutocompute(const TD::OP_Inputs* inputs)
{
	return inputs->getParInt(BatchAutocomputeName) != 0;
}

inline int evalBatchFftsize(const TD::OP_Inputs* inputs)
{
	const char* val = inputs->getParString(BatchFftsizeName);
	if (!val || val[0] == '\0') return 2048;
	int v = std::atoi(val);
	return (v > 0) ? v : 2048;
}

inline int evalBatchHopsize(const TD::OP_Inputs* inputs)
{
	int v = inputs->getParInt(BatchHopsizeName);
	return (v > 0) ? v : 1024;
}

inline std::string evalBatchWindowtype(const TD::OP_Inputs* inputs)
{
	const char* val = inputs->getParString(BatchWindowtypeName);
	if (!val || val[0] == '\0') return "blackmanharris62";
	return std::string(val);
}

inline int evalBatchZeropadding(const TD::OP_Inputs* inputs)
{
	const char* val = inputs->getParString(BatchZeropaddingName);
	if (!val || val[0] == '\0') return 0;
	return std::atoi(val);
}

// ---------------------------------------------------------------------------
// Helper: convert zero-padding factor (0/1/2) to absolute sample count
// ---------------------------------------------------------------------------

inline int zeroPadFromFactor(int zeroPadFactor, int fftSize)
{
	if (zeroPadFactor == 1) return fftSize / 2;
	if (zeroPadFactor == 2) return fftSize;
	return 0;
}

// ---------------------------------------------------------------------------
// Helper: warn when hop > FFT size — frames are sparse, audio between
// frames is skipped. Not clamped: output sampleRate is derived from the
// unclamped hop, so clamping here would desync numFrames from that rate.
// ---------------------------------------------------------------------------

inline std::string batchFramingWarning(int fftSize, int hopSize)
{
	return (hopSize > fftSize)
		? "Hop size exceeds FFT size — analysis frames are sparse (audio between frames is skipped)"
		: std::string();
}

} // namespace EssentiaTD
