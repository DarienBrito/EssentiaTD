// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Two realtime guards on the SHIPPED EssentiaSpectralCHOP execute path:
//   1. spectral_flux stays a frame-to-frame difference across a parameter
//      change. Every reconfigure builds a new Essentia Flux, whose first
//      compute returns the spectrum's own L2 norm instead of a difference.
//   2. with every feature disabled the op writes its declared channel rather
//      than leaving whatever the output buffer already held.

#include "TDStubs.h"
#include "EssentiaSpectralCHOP.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using EssentiaTD::EssentiaSpectralCHOP;
using EssentiaTDTest::DriveResult;
using EssentiaTDTest::FakeInputs;
using EssentiaTDTest::driveRealtime;

namespace
{

int gFailures = 0;

void report(const char* what, bool ok, const char* detail)
{
	std::printf("%-46s %-28s %s\n", what, detail, ok ? "PASS" : "FAIL");
	if (!ok) ++gFailures;
}

std::vector<float> sine(double hz, double seconds, double sampleRate)
{
	std::vector<float> a(static_cast<size_t>(seconds * sampleRate));
	for (size_t i = 0; i < a.size(); ++i)
		a[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * hz * i / sampleRate));
	return a;
}

/// Flux only, so the flux value is channel 0.
void setFluxOnly(FakeInputs& in, double rolloffCutoff)
{
	in.str["Mode"]          = "realtime";
	in.str["Fftsize"]       = "1024";
	in.str["Windowtype"]    = "hann";
	in.integer["Enableflux"] = 1;
	in.dbl["Rolloffcutoff"]  = rolloffCutoff;
}

} // namespace

int main()
{
	const double sampleRate = 44100.0;
	const int    timeslice  = 735;
	const std::vector<float> tone = sine(440.0, 2.0, sampleRate);

	// ---- 1. flux across a reconfigure ------------------------------------
	{
		EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		setFluxOnly(in, 0.85);

		DriveResult steady = driveRealtime(chop, in, tone, timeslice, sampleRate);
		const float steadyFlux = steady.values.empty() ? -1.0f : steady.values[0];

		// Change a parameter that forces a reconfigure, then cook exactly once
		// so the observed value IS the first cook after the rebuild.
		in.dbl["Rolloffcutoff"] = 0.90;
		const std::vector<float> oneSlice(tone.begin(), tone.begin() + timeslice);
		DriveResult after = driveRealtime(
			chop, in, oneSlice, timeslice, sampleRate,
			static_cast<int64_t>(tone.size() / timeslice));
		const float afterFlux = after.values.empty() ? -1.0f : after.values[0];

		char detail[128];
		std::snprintf(detail, sizeof(detail), "steady %.6f -> after %.6f",
		              steadyFlux, afterFlux);

		// A rebuilt Flux reports ||S||, orders of magnitude above a steady-tone
		// difference. Anything near the steady scale means it was primed.
		report("flux survives a parameter change",
		       afterFlux >= 0.0f && afterFlux <= steadyFlux * 5.0f + 1e-4f, detail);
	}

	// ---- 2. no features enabled ------------------------------------------
	{
		EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		in.str["Mode"]       = "realtime";
		in.str["Fftsize"]    = "1024";
		in.str["Windowtype"] = "hann";
		// every Enable* left unset, i.e. false

		DriveResult r = driveRealtime(chop, in, tone, timeslice, sampleRate);

		char detail[128];
		std::snprintf(detail, sizeof(detail), "%d ch, value %.6f, warn \"%s\"",
		              r.numChannels, r.values.empty() ? -1.0f : r.values[0],
		              r.warning.c_str());

		const bool zeroed = !r.values.empty() && r.values[0] == 0.0f;
		const bool warned = r.warning.find("No features enabled") != std::string::npos;
		report("all features off: channel zeroed + warned", zeroed && warned, detail);
	}

	std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED",
	            gFailures, gFailures == 1 ? "" : "s");
	return gFailures ? 1 : 0;
}
