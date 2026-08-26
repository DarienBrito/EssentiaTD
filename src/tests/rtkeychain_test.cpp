// SPDX-License-Identifier: AGPL-3.0-or-later
//
// rtkeychain_test — drives the REAL realtime path of EssentiaTonalCHOP without
// TouchDesigner, by stubbing the handful of OP_Inputs methods the plugin calls
// (getParDouble/Int/String, getInputCHOP, getTimeInfo, enablePar).
//
// Exists because the key-parity fix (issue #12) was measured offline through
// the BATCH path only. Realtime shares the algorithm chain but has wiring the
// batch path does not: the ring buffer, the Key Frames deque, and the ordering
// of whitening -> key chroma -> pre-correlation processing. This exercises that
// code as shipped rather than mirroring it.
//
// Asserts:
//   1. RT recovers the constructed tonic of a synthetic i-iv-V-i progression.
//   2. RT agrees with the batch path on identical audio (mode parity).
//   3. key_strength is finite and non-zero.

#include "EssentiaTonalCHOP.h"
#include "Shared/BatchCommon.h"
#include "Parameters_Tonal.h"
#include "tests/TDStubs.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace TD;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kTimeslice  = 800;      // 48k / 60fps
constexpr int    kFftSize    = 4096;

// ---------------------------------------------------------------------------
// Synthetic audio: i - iv - V - i in a chosen minor key, harmonic-rich enough
// for SpectralPeaks to find partials. Ground truth is by construction.
// ---------------------------------------------------------------------------
std::vector<float> renderMinorProgression(int tonicPc, double seconds)
{
	// scale degrees of the triads, semitones above the tonic
	const int chords[4][3] = {
		{0, 3, 7},    // i
		{5, 8, 12},   // iv
		{7, 11, 14},  // V (major, harmonic minor)
		{0, 3, 7},    // i
	};
	const size_t total = static_cast<size_t>(seconds * kSampleRate);
	std::vector<float> out(total, 0.0f);
	const size_t perChord = total / 4;

	for (int c = 0; c < 4; ++c)
	{
		for (int v = 0; v < 3; ++v)
		{
			// A4 = 440 is pitch class 9; place the chord in a middle octave
			const int    pc   = tonicPc + chords[c][v];
			const double freq = 440.0 * std::pow(2.0, (pc - 9 - 12) / 12.0);
			for (int h = 1; h <= 4; ++h)     // 4 partials, 1/h amplitude
			{
				const double f = freq * h;
				if (f > kSampleRate / 2.0) break;
				const double amp = 0.22 / h;
				for (size_t i = 0; i < perChord; ++i)
				{
					const size_t n = c * perChord + i;
					if (n >= total) break;
					// short fade so chord edges are not clicks
					double env = 1.0;
					const double fade = 0.02 * kSampleRate;
					if (i < fade)             env = i / fade;
					else if (i > perChord - fade) env = (perChord - i) / fade;
					out[n] += static_cast<float>(
						amp * env * std::sin(2.0 * M_PI * f * i / kSampleRate));
				}
			}
		}
	}
	return out;
}

using EssentiaTDTest::CaptureString;
using EssentiaTDTest::FakeInputs;

const char* kNotes[12] = { "C", "C#", "D", "D#", "E", "F",
                           "F#", "G", "G#", "A", "A#", "B" };

int failures = 0;

void check(bool ok, const std::string& what)
{
	std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok) ++failures;
}

// ---------------------------------------------------------------------------
// Drive the shipped realtime path over the whole buffer, one timeslice per cook
// ---------------------------------------------------------------------------
struct RtResult { int keyIdx; int minor; float strength; };

RtResult runRealtime(const std::vector<float>& audio, int keyFrames,
                     double smoothing = 0.0, bool enableHpcp = false)
{
	EssentiaTD::EssentiaTonalCHOP chop(nullptr);

	FakeInputs in;
	in.str[EssentiaTD::ModeName]            = "realtime";
	in.str[EssentiaTD::BatchFftsizeName]    = std::to_string(kFftSize);
	in.str[EssentiaTD::BatchWindowtypeName] = "blackmanharris62";
	in.str[EssentiaTD::KeyprofileName]      = "bgate";
	in.str[EssentiaTD::HpcpnormalizedName]  = "unitMax";
	in.str[EssentiaTD::PitchalgoName]       = "yinfft";
	in.integer[EssentiaTD::EnablekeyName]             = 1;
	in.integer[EssentiaTD::EnablehpcpName]            = enableHpcp ? 1 : 0;
	in.integer[EssentiaTD::EnablepitchName]           = 0;
	in.integer[EssentiaTD::EnabledissonanceName]      = 0;
	in.integer[EssentiaTD::EnableinharmonicityName]   = 0;
	in.integer[EssentiaTD::HpcpsizeName]              = 12;
	in.integer[EssentiaTD::HpcpharmonicsName]         = 0;
	in.integer[EssentiaTD::KeyframesName]             = keyFrames;
	in.dbl[EssentiaTD::SmoothingName]      = smoothing;
	in.dbl[EssentiaTD::PeakthresholdName]  = 0.00001;
	in.dbl[EssentiaTD::PeakmaxfreqName]    = 3500.0;
	in.dbl[EssentiaTD::ReferencefreqName]  = 440.0;
	// configureAlgorithms() builds every algorithm regardless of which features
	// are enabled, so keep the pitch parameters valid even with pitch off. A
	// zero here now costs only PitchYinFFT (the creates are guarded per
	// algorithm); this test is about the key chain, so it stays out of the way.
	// The guard itself is covered by tonalconfigfail_test.
	in.dbl[EssentiaTD::PitchminfreqName]   = 20.0;
	in.dbl[EssentiaTD::PitchmaxfreqName]   = 22050.0;
	in.dbl[EssentiaTD::PitchtoleranceName] = 1.0;

	RtResult r{ -1, -1, 0.0f };
	const size_t cooks = audio.size() / kTimeslice;

	for (size_t c = 0; c < cooks; ++c)
	{
		const float*  slice   = audio.data() + c * kTimeslice;
		const float*  chans[] = { slice };
		const char*   names[] = { "chan1" };

		OP_CHOPInput input{};
		input.opPath      = "/audio";
		input.opId        = 1;
		input.numChannels = 1;
		input.numSamples  = kTimeslice;
		input.sampleRate  = kSampleRate;
		input.startIndex  = 0;
		input.channelData = chans;
		input.nameData    = names;
		input.totalCooks  = static_cast<int64_t>(c) + 1;
		in.chop           = &input;
		in.timeInfo.absFrame = static_cast<int64_t>(c);

		CHOP_OutputInfo info{};
		info.numChannels = 0;
		chop.getOutputInfo(&info, &in, nullptr);
		if (info.numChannels <= 0) continue;

		std::vector<std::vector<float>> store(info.numChannels,
			std::vector<float>(info.numSamples > 0 ? info.numSamples : 1, 0.0f));
		std::vector<float*> ptrs;
		for (auto& v : store) ptrs.push_back(v.data());
		std::vector<const char*> chName(info.numChannels, "ch");

		CHOP_Output out(info.numChannels,
		                info.numSamples > 0 ? info.numSamples : 1,
		                info.sampleRate, info.startIndex,
		                ptrs.data(), chName.data());
		chop.execute(&out, &in, nullptr);

		// pitch is off, so the key triple follows the hpcp block when present
		const int keyBase = enableHpcp ? 12 : 0;
		if (info.numChannels >= keyBase + 3)
		{
			r.keyIdx   = static_cast<int>(std::lround(store[keyBase][0]));
			r.minor    = static_cast<int>(std::lround(store[keyBase + 1][0]));
			r.strength = store[keyBase + 2][0];
		}

		if (c + 1 == cooks)
		{
			CaptureString warn, err;
			chop.getWarningString(&warn, nullptr);
			chop.getErrorString(&err, nullptr);
			if (!warn.value.empty()) std::printf("  warning: %s\n", warn.value.c_str());
			if (!err.value.empty())  std::printf("  error  : %s\n", err.value.c_str());
		}
	}
	return r;
}

RtResult runBatch(const std::vector<float>& audio)
{
	EssentiaTD::AudioSnapshot snap;
	snap.data.assign(audio.begin(), audio.end());
	snap.sampleRate = kSampleRate;

	EssentiaTD::BatchTonalParams p{};
	p.enableKey     = true;
	p.enableHpcp    = false;
	p.enablePitch   = false;
	p.enableDiss    = false;
	p.enableInharm  = false;
	p.hpcpSize      = 12;
	p.hpcpHarmonics = 0;
	p.hpcpNormalized = 0;
	p.hpcpNonLinear = false;
	p.keyMode       = 0;              // global
	p.keyProfile    = 0;              // bgate
	p.fftSize       = kFftSize;
	p.hopSize       = kFftSize / 2;
	p.windowType    = "blackmanharris62";
	p.zeroPad       = 0;
	p.peakThreshold = 0.00001f;
	p.peakMaxFreq   = 3500.0f;
	p.referenceFreq = 440.0f;

	std::atomic<bool>  cancel{ false };
	std::atomic<float> progress{ 0.0f };
	auto res = EssentiaTD::EssentiaTonalCHOP::computeBatchAsync(snap, p, cancel, progress);

	RtResult r{ -1, -1, 0.0f };
	if (res.success && res.cache.size() >= 3 && !res.cache[0].empty())
	{
		r.keyIdx   = static_cast<int>(std::lround(res.cache[0][0]));
		r.minor    = static_cast<int>(std::lround(res.cache[1][0]));
		r.strength = res.cache[2][0];
	}
	return r;
}

} // namespace

int main()
{
	std::printf("rtkeychain_test — realtime key path, no TouchDesigner\n");

	// Several tonics, including one that decodes as a sharp, so a single lucky
	// pass cannot carry the test and enharmonic handling is exercised.
	const int tonics[] = { 2, 7, 10 };   // D, G, A#

	for (int tonic : tonics)
	{
		const std::vector<float> audio = renderMinorProgression(tonic, 4.8);

		// Key Frames is capped at 300, which is 5 s at 60 fps, so the material
		// is sized to fit inside one window. Otherwise realtime would correctly
		// report the harmony under the playhead rather than the piece, and the
		// test would be measuring window length, not the key chain. (The 8-frame
		// default, 0.341 s, is a separate open question.)
		const RtResult rt = runRealtime(audio, 300);
		const RtResult ba = runBatch(audio);

		std::printf("  %s minor -> realtime %s %s (%.4f) | batch %s %s (%.4f)\n",
			kNotes[tonic],
			rt.keyIdx >= 0 && rt.keyIdx < 12 ? kNotes[rt.keyIdx] : "?",
			rt.minor == 1 ? "minor" : "major", rt.strength,
			ba.keyIdx >= 0 && ba.keyIdx < 12 ? kNotes[ba.keyIdx] : "?",
			ba.minor == 1 ? "minor" : "major", ba.strength);

		check(rt.keyIdx == tonic && rt.minor == 1,
		      std::string("realtime recovers the constructed tonic (")
		          + kNotes[tonic] + " minor)");
		check(ba.keyIdx == tonic && ba.minor == 1,
		      std::string("batch recovers the constructed tonic (")
		          + kNotes[tonic] + " minor)");
		check(rt.keyIdx == ba.keyIdx && rt.minor == ba.minor,
		      "realtime and batch agree on identical audio");
		check(std::isfinite(rt.strength) && rt.strength > 0.0f,
		      "realtime key_strength is finite and non-zero");

		// The Smoothing EMA is a display filter for the hpcp_* channels. It used
		// to be applied in place to the buffer the key accumulator read, so a
		// chroma display setting silently changed key output. The key stage now
		// runs its own chroma, and this pins that: identical key AND identical
		// strength with Smoothing at 0.9 and Enable HPCP on.
		const RtResult sm = runRealtime(audio, 300, 0.9, true);
		check(sm.keyIdx == rt.keyIdx && sm.minor == rt.minor
		          && sm.strength == rt.strength,
		      "key output is independent of Smoothing / Enable HPCP");
	}

	std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
	return failures ? 1 : 0;
}
