// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tonalconfigfail_test — pins the realtime config-failure lifecycle of
// EssentiaTonalCHOP against a parameter Essentia actually rejects, driven
// through the shipped execute() path (TDStubs.h).
//
// Two behaviours, and they pull in opposite directions:
//   1. PARTIAL failure is a warning. configureAlgorithms() guards each create,
//      so a zero Pitch Min Freq disables pitch alone. Before the guards a
//      single rejected parameter aborted the whole construction and took key,
//      chroma, dissonance and inharmonicity down with it — silently, since
//      pitch was not even enabled.
//   2. TOTAL failure is an error that never lets go on its own. A zero Peak
//      Max Freq kills SpectralPeaks, so nothing enabled downstream of it can
//      run. execute() clears myError each cook, so a failure that is not
//      re-applied from the persistent slot shows for one frame and the op then
//      looks healthy while emitting zeros.

#include "EssentiaTonalCHOP.h"
#include "Parameters_Tonal.h"
#include "tests/TDStubs.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using EssentiaTDTest::DriveResult;
using EssentiaTDTest::FakeInputs;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kTimeslice  = 800;      // 48k / 60fps
constexpr int    kFftSize    = 4096;

int failures = 0;

void check(bool ok, const std::string& what)
{
	std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok) ++failures;
}

bool contains(const std::string& hay, const char* needle)
{
	return hay.find(needle) != std::string::npos;
}

// A sustained minor triad with four partials per voice — harmonic enough for
// SpectralPeaks to find something, which is all these assertions need.
std::vector<float> renderTriad(double seconds)
{
	const size_t total = static_cast<size_t>(seconds * kSampleRate);
	std::vector<float> out(total, 0.0f);
	const int semis[3] = { 0, 3, 7 };            // A minor, root at A3

	for (int v = 0; v < 3; ++v)
	{
		const double root = 220.0 * std::pow(2.0, semis[v] / 12.0);
		for (int h = 1; h <= 4; ++h)
		{
			const double f = root * h;
			if (f > kSampleRate / 2.0) break;
			const double amp = 0.22 / h;
			for (size_t i = 0; i < total; ++i)
				out[i] += static_cast<float>(
					amp * std::sin(2.0 * M_PI * f * i / kSampleRate));
		}
	}
	return out;
}

// Baseline parameters: key only, everything else off. Individual tests then
// poison one value.
void setBaseline(FakeInputs& in)
{
	in.str[EssentiaTD::ModeName]            = "realtime";
	in.str[EssentiaTD::BatchFftsizeName]    = std::to_string(kFftSize);
	in.str[EssentiaTD::BatchWindowtypeName] = "blackmanharris62";
	in.str[EssentiaTD::KeyprofileName]      = "bgate";
	in.str[EssentiaTD::HpcpnormalizedName]  = "unitMax";
	in.str[EssentiaTD::PitchalgoName]       = "yinfft";
	in.integer[EssentiaTD::EnablekeyName]           = 1;
	in.integer[EssentiaTD::EnablehpcpName]          = 0;
	in.integer[EssentiaTD::EnablepitchName]         = 0;
	in.integer[EssentiaTD::EnabledissonanceName]    = 0;
	in.integer[EssentiaTD::EnableinharmonicityName] = 0;
	in.integer[EssentiaTD::HpcpharmonicsName]       = 0;
	in.integer[EssentiaTD::KeyframesName]           = 300;
	in.dbl[EssentiaTD::SmoothingName]      = 0.0;
	in.dbl[EssentiaTD::PeakthresholdName]  = 0.00001;
	in.dbl[EssentiaTD::PeakmaxfreqName]    = 3500.0;
	in.dbl[EssentiaTD::ReferencefreqName]  = 440.0;
	in.dbl[EssentiaTD::PitchminfreqName]   = 20.0;
	in.dbl[EssentiaTD::PitchmaxfreqName]   = 22050.0;
	in.dbl[EssentiaTD::PitchtoleranceName] = 1.0;
}

void report(const char* label, const DriveResult& r)
{
	std::printf("  %s: %d cooks, %d with error (first at %d, %d gaps after), "
	            "%d channels\n",
	            label, r.cooks, r.cooksWithError, r.firstErrorCook,
	            r.errorGaps, r.numChannels);
	if (!r.warning.empty()) std::printf("    warning: %s\n", r.warning.c_str());
	if (!r.error.empty())   std::printf("    error  : %s\n", r.error.c_str());
}

} // namespace

int main()
{
	std::printf("tonalconfigfail_test — realtime config-failure lifecycle\n");

	const std::vector<float> audio = renderTriad(3.0);

	// --- healthy baseline: no error, no algorithm warning ------------------
	{
		EssentiaTD::EssentiaTonalCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("healthy", r);

		check(r.cooks > 0, "healthy: the op cooked");
		check(r.cooksWithError == 0, "healthy: no cook reports an error");
		check(!contains(r.warning, "Algorithm config failed"),
		      "healthy: no algorithm-config warning");
		check(r.numChannels == 3 && r.values.size() == 3 && r.values[2] > 0.0f,
		      "healthy: key_strength is non-zero");
	}

	// --- partial failure: pitch is off AND its parameter is invalid --------
	// Only PitchYinFFT rejects a zero minFrequency ((0,inf) in pitchyinfft.h).
	// Everything the key chain needs must still be built.
	{
		EssentiaTD::EssentiaTonalCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.dbl[EssentiaTD::PitchminfreqName] = 0.0;
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("partial", r);

		check(r.cooksWithError == 0,
		      "partial: a failure outside every enabled feature is not an error");
		check(contains(r.warning, "PitchYinFFT"),
		      "partial: the warning names the algorithm that failed");
		check(r.numChannels == 3 && r.values.size() == 3 && r.values[2] > 0.0f,
		      "partial: the key chain still produces a non-zero key_strength");
	}

	// --- total failure: the only enabled feature loses its chain -----------
	// SpectralPeaks, SpectralWhitening and the key HPCP all declare
	// maxFrequency as (0,inf), so a zero Peak Max Freq takes the whole key
	// chain out. Key is the only feature enabled, so the op has nothing left.
	{
		EssentiaTD::EssentiaTonalCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.dbl[EssentiaTD::PeakmaxfreqName] = 0.0;
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("total", r);

		check(r.firstErrorCook > 0 && r.errorGaps == 0,
		      "total: once the error appears it never clears itself");
		check(r.cooksWithError > 1,
		      "total: the error survives well past the cook that raised it");
		check(contains(r.error, "No enabled feature could be configured"),
		      "total: the error says nothing enabled could be configured");
		check(contains(r.error, "SpectralPeaks"),
		      "total: the error names the algorithm that failed");

		bool allZero = true;
		for (float v : r.values) if (v != 0.0f) allZero = false;
		check(allZero, "total: output is zeroed");
	}

	// --- recovery: fixing the parameter clears the error -------------------
	// The persistent slot is what keeps the error on display, so it also has
	// to let go. Same CHOP instance across both drives.
	{
		EssentiaTD::EssentiaTonalCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.dbl[EssentiaTD::PeakmaxfreqName] = 0.0;
		const DriveResult bad = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		check(bad.firstErrorCook > 0 && bad.errorGaps == 0,
		      "recovery: the error is on display before the fix");

		in.dbl[EssentiaTD::PeakmaxfreqName] = 3500.0;
		const DriveResult good = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate,
			static_cast<int64_t>(bad.cooks));
		report("recovery", good);

		check(good.cooksWithError == 0,
		      "recovery: fixing the parameter clears the error");
		check(good.values.size() == 3 && good.values[2] > 0.0f,
		      "recovery: key_strength returns");
	}

	std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
	return failures ? 1 : 0;
}
