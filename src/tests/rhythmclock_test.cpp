// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rhythm realtime clock. Drives the SHIPPED executeRealtimeImpl over one click
// track twice: once with the timeslice TD delivers when it hits the target
// rate, once with the longer timeslice it delivers when frames are dropped.
// The reported tempo must not move — the ODF clock has to come from elapsed
// audio, not from a cook count against the nominal timeline rate. Also asserts
// that silence holds the last tempo instead of tracking an empty buffer.

#include "TDStubs.h"
#include "EssentiaRhythmCHOP.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using EssentiaTD::EssentiaRhythmCHOP;
using EssentiaTDTest::DriveResult;
using EssentiaTDTest::FakeInputs;
using EssentiaTDTest::driveRealtime;

namespace
{

int gFailures = 0;

void check(const char* what, bool ok, double got, double want, double tol)
{
	std::printf("%-44s got %8.3f  want %8.3f +/- %-6.2f %s\n",
	            what, got, want, tol, ok ? "PASS" : "FAIL");
	if (!ok) ++gFailures;
}

// Output channel order: onset, onset_strength, bpm, beat, beat_phase, beat_confidence
constexpr int kBpm        = 2;
constexpr int kConfidence = 5;

/// One short decaying 1.2 kHz burst per beat.
std::vector<float> clickTrack(double bpm, double seconds, double sampleRate)
{
	std::vector<float> a(static_cast<size_t>(seconds * sampleRate), 0.0f);
	const double period = 60.0 / bpm * sampleRate;
	const int    burst  = static_cast<int>(0.010 * sampleRate);

	for (double start = 0.0;
	     static_cast<size_t>(start) + burst < a.size();
	     start += period)
	{
		for (int i = 0; i < burst; ++i)
		{
			const double env = 1.0 - static_cast<double>(i) / burst;
			a[static_cast<size_t>(start) + i] = static_cast<float>(
				env * env * std::sin(2.0 * M_PI * 1200.0 * i / sampleRate));
		}
	}
	return a;
}

void setRealtimeParams(FakeInputs& in)
{
	in.str["Mode"]             = "realtime";
	in.str["Onsetmethod"]      = "complex";
	in.str["Rtwindowsize"]     = "1024";
	in.str["Windowtype"]       = "hann";
	in.dbl["Onsetsensitivity"] = 0.5;
	in.integer["Bpmmin"]       = 60;
	in.integer["Bpmmax"]       = 180;
	// timeInfo.rate stays 60: the timeline rate is what the op is told, and
	// dropping frames does not change it. That is the whole point.
}

float drive(const std::vector<float>& audio, int timeslice, double sampleRate,
            float* confidenceOut = nullptr)
{
	EssentiaRhythmCHOP chop(nullptr);
	FakeInputs in;
	setRealtimeParams(in);

	DriveResult r = driveRealtime(chop, in, audio, timeslice, sampleRate);
	if (static_cast<int>(r.values.size()) <= kConfidence) return -1.0f;

	if (confidenceOut) *confidenceOut = r.values[kConfidence];
	return r.values[kBpm];
}

} // namespace

int main()
{
	const double sampleRate = 44100.0;
	const double trueBpm    = 120.0;
	const std::vector<float> audio = clickTrack(trueBpm, 20.0, sampleRate);

	// 735 samples/cook = 44100/60, the timeslice when TD holds 60 fps.
	const float bpmOnRate = drive(audio, 735, sampleRate);
	check("60 fps: bpm tracks the click track",
	      std::fabs(bpmOnRate - trueBpm) <= 8.0, bpmOnRate, trueBpm, 8.0);

	// 882 samples/cook = 44100/50. TD lengthens the timeslice when frames are
	// dropped while getTimeInfo()->rate still reads 60, so a cook-count clock
	// reports 60/50 = 1.2x high (120 bpm read as 144).
	const float bpmDropped = drive(audio, 882, sampleRate);
	check("50 fps (dropped frames): same bpm",
	      std::fabs(bpmDropped - bpmOnRate) <= 4.0, bpmDropped, bpmOnRate, 4.0);

	// 1470 samples/cook = 44100/30: every other frame dropped.
	const float bpmHalfRate = drive(audio, 1470, sampleRate);
	check("30 fps (half rate): same bpm",
	      std::fabs(bpmHalfRate - bpmOnRate) <= 8.0, bpmHalfRate, bpmOnRate, 8.0);

	// Silence longer than the 512-frame ODF history (8.5 s at 60 fps). The
	// tracker max-normalizes its input, so an ungated buffer of silence gets
	// tracked as if it were music and the tempo pins to Bpmmax.
	{
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setRealtimeParams(in);

		driveRealtime(chop, in, audio, 735, sampleRate);
		int64_t cooks = static_cast<int64_t>(audio.size() / 735);

		// First stretch: the history still carries the last of the music, so
		// the estimate may legitimately settle somewhere new.
		const std::vector<float> silence(static_cast<size_t>(12.0 * sampleRate), 0.0f);
		DriveResult fading = driveRealtime(chop, in, silence, 735, sampleRate, cooks);
		cooks += static_cast<int64_t>(silence.size() / 735);

		// Second stretch: the 512-frame history is now nothing but silence.
		// Whatever the tempo reads, it must stop moving.
		DriveResult quiet = driveRealtime(chop, in, silence, 735, sampleRate, cooks);

		check("silence: bpm stops moving once fully quiet",
		      std::fabs(quiet.values[kBpm] - fading.values[kBpm]) < 0.01,
		      quiet.values[kBpm], fading.values[kBpm], 0.01);
		check("silence: bpm stays near the music tempo",
		      std::fabs(quiet.values[kBpm] - trueBpm) <= 15.0,
		      quiet.values[kBpm], trueBpm, 15.0);
		check("silence: bpm does not pin to Bpmmax",
		      quiet.values[kBpm] < 179.0, quiet.values[kBpm], 0.0, 179.0);
		check("silence: confidence drops to 0",
		      quiet.values[kConfidence] == 0.0f, quiet.values[kConfidence], 0.0, 0.0);
	}

	std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED",
	            gFailures, gFailures == 1 ? "" : "s");
	return gFailures ? 1 : 0;
}
