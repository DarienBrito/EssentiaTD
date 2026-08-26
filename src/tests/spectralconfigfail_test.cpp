// SPDX-License-Identifier: AGPL-3.0-or-later
//
// spectralconfigfail_test — pins the realtime config-failure lifecycle of
// EssentiaSpectralCHOP, driven through the shipped execute() path (TDStubs.h).
//
// configureAlgorithms() has guarded each create for a while, so one bad feature
// only disables itself. What was missing is the other end of the scale: with
// every enabled feature dead the op emitted a full frame of zeros carrying only
// a warning, which reads as healthy-but-silent. That case is now an error, and
// it has to be re-applied every cook (execute() clears myError each time).
//
// The lever is MelBands: "the number of spectrum bins is insufficient for the
// specified number of bands" whenever the requested bands cannot each claim a
// bin over the requested frequency span.

#include "EssentiaSpectralCHOP.h"
#include "Parameters_Spectral.h"
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
constexpr int    kFftSize    = 1024;     // 513 bins

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

std::vector<float> renderTone(double seconds)
{
	const size_t total = static_cast<size_t>(seconds * kSampleRate);
	std::vector<float> out(total, 0.0f);
	for (int h = 1; h <= 6; ++h)
	{
		const double f   = 220.0 * h;
		const double amp = 0.15 / h;
		for (size_t i = 0; i < total; ++i)
			out[i] += static_cast<float>(
				amp * std::sin(2.0 * M_PI * f * i / kSampleRate));
	}
	return out;
}

// Baseline: mel bands only, at a band count the FFT can support.
void setBaseline(FakeInputs& in)
{
	in.str[EssentiaTD::ModeName]            = "realtime";
	in.str[EssentiaTD::BatchFftsizeName]    = std::to_string(kFftSize);
	in.str[EssentiaTD::BatchWindowtypeName] = "blackmanharris62";
	in.str[EssentiaTD::MelbandscountName]   = "24";
	in.str[EssentiaTD::ContrastbandsName]   = "6";
	in.str[EssentiaTD::HfctypeName]         = "Masri";
	in.str[EssentiaTD::FluxnormName]        = "L2";
	// configureAlgorithms() builds every feature regardless of its enable, so
	// MFCC's count has to be valid even with MFCC off
	in.integer[EssentiaTD::MffcccountName]       = 13;
	in.integer[EssentiaTD::EnablemelName]        = 1;
	in.integer[EssentiaTD::EnablemfccName]       = 0;
	in.integer[EssentiaTD::EnablecentroidName]   = 0;
	in.integer[EssentiaTD::EnablefluxName]       = 0;
	in.integer[EssentiaTD::EnablerolloffName]    = 0;
	in.integer[EssentiaTD::EnablecontrastName]   = 0;
	in.integer[EssentiaTD::EnablehfcName]        = 0;
	in.integer[EssentiaTD::EnablecomplexityName] = 0;
	in.integer[EssentiaTD::EnablepcaName]        = 0;
	in.dbl[EssentiaTD::MellowfreqName]  = 0.0;
	in.dbl[EssentiaTD::MelhighfreqName] = 8000.0;
	in.dbl[EssentiaTD::MfcclowfreqName]  = 0.0;
	in.dbl[EssentiaTD::MfcchighfreqName] = 8000.0;
	in.dbl[EssentiaTD::RolloffcutoffName]    = 0.85;
	in.dbl[EssentiaTD::ComplexitythreshName] = 0.005;
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

bool anyNonZero(const std::vector<float>& v)
{
	for (float x : v) if (x != 0.0f) return true;
	return false;
}

} // namespace

int main()
{
	std::printf("spectralconfigfail_test — realtime config-failure lifecycle\n");

	const std::vector<float> audio = renderTone(2.0);

	// --- healthy baseline --------------------------------------------------
	{
		EssentiaTD::EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("healthy", r);

		check(r.cooks > 0, "healthy: the op cooked");
		check(r.cooksWithError == 0, "healthy: no cook reports an error");
		check(!contains(r.warning, "Feature config failed"),
		      "healthy: no feature-config warning");
		check(r.numChannels == 24 && anyNonZero(r.values),
		      "healthy: 24 mel bands, non-zero");
	}

	// --- partial failure: one dead feature, one live -----------------------
	// 512 mel bands over 0-8000 Hz cannot fit in 513 bins spanning 0-24 kHz,
	// so MelBands is rejected. Centroid is enabled and unaffected, so this
	// stays a warning.
	{
		EssentiaTD::EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.str[EssentiaTD::MelbandscountName]      = "512";
		in.integer[EssentiaTD::EnablecentroidName] = 1;
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("partial", r);

		check(r.cooksWithError == 0,
		      "partial: one dead feature among live ones is not an error");
		check(contains(r.warning, "MelBands"),
		      "partial: the warning names the feature that failed");
		// channel order is mfcc, centroid, ... , mel: centroid leads here
		check(r.numChannels == 513 && r.values.size() == 513
		          && r.values[0] > 0.0f,
		      "partial: centroid still produces a value");
	}

	// --- total failure: the only enabled feature is the dead one -----------
	{
		EssentiaTD::EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.str[EssentiaTD::MelbandscountName] = "512";
		const DriveResult r = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		report("total", r);

		check(r.firstErrorCook > 0 && r.errorGaps == 0,
		      "total: once the error appears it never clears itself");
		check(r.cooksWithError > 1,
		      "total: the error survives well past the cook that raised it");
		check(contains(r.error, "No enabled feature could be configured"),
		      "total: the error says nothing enabled could be configured");
		check(contains(r.error, "MelBands"),
		      "total: the error names the feature that failed");
		check(!anyNonZero(r.values), "total: output is zeroed");
	}

	// --- recovery: fixing the band count clears the error ------------------
	{
		EssentiaTD::EssentiaSpectralCHOP chop(nullptr);
		FakeInputs in;
		setBaseline(in);
		in.str[EssentiaTD::MelbandscountName] = "512";
		const DriveResult bad = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate);
		check(bad.firstErrorCook > 0 && bad.errorGaps == 0,
		      "recovery: the error is on display before the fix");

		in.str[EssentiaTD::MelbandscountName] = "24";
		const DriveResult good = EssentiaTDTest::driveRealtime(
			chop, in, audio, kTimeslice, kSampleRate,
			static_cast<int64_t>(bad.cooks));
		report("recovery", good);

		check(good.cooksWithError == 0,
		      "recovery: fixing the band count clears the error");
		check(good.numChannels == 24 && anyNonZero(good.values),
		      "recovery: the mel bands return");
	}

	std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
	return failures ? 1 : 0;
}
