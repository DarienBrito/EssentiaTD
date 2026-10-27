// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rhythm drum detector. Drives the SHIPPED realtime and batch paths over a
// synthetic kit (kick on beats, snare on 2 and 4, closed hat on off-beats) and
// asserts per-class precision and recall at +/-50 ms, that the rhythm
// channels stay bit-identical with Enable Drums on, that levels stay in 0-1,
// and that digital silence never triggers.
// Guards (each mutation-checked): band routing per class, refractory,
// relative floor, level gate, rhythm channels untouched (both modes), drum
// channels alive during onset warm-up, empty-band warning, channel layout,
// batch drum timing independent of FFT Size.
// Does not guard: the same-hit rule (synthetic decays give ~0 flux, so it
// never binds here), batch latency per drum window (instant synthetic attacks
// cannot resolve it) or accuracy on real music (diag/drum-detect's corpus).
// The first 0.6 s are not scored: running peaks start at 0, so the first hits
// may fire any class (cold start, same in the tuned reference).

#include "TDStubs.h"
#include "EssentiaRhythmCHOP.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using EssentiaTD::EssentiaRhythmCHOP;
using EssentiaTDTest::FakeInputs;
using EssentiaTDTest::driveRealtime;

namespace
{

int gFailures = 0;

void check(const char* what, bool ok, double got, double want)
{
	std::printf("%-52s got %8.3f  want %8.3f %s\n", what, got, want, ok ? "PASS" : "FAIL");
	if (!ok) ++gFailures;
}

constexpr double kSr     = 44100.0;
constexpr int    kSlice  = 735;      // 60 fps
constexpr int    kRhythm = EssentiaRhythmCHOP::kNumOutputChannels;
constexpr int    kLevel  = EssentiaRhythmCHOP::kDrumLevelBase;    // low mid high
constexpr int    kTrig   = EssentiaRhythmCHOP::kDrumTriggerBase;  // kick perc hat

struct Biquad
{
	double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
	static Biquad make(bool highpass, bool bandpass, double f, double q)
	{
		const double w = 2 * M_PI * f / kSr, c = std::cos(w), al = std::sin(w) / (2 * q);
		double b0, b1, b2;
		if (bandpass)      { b0 = al;            b1 = 0;            b2 = -al; }
		else if (highpass) { b0 = (1 + c) / 2;   b1 = -(1 + c);     b2 = (1 + c) / 2; }
		else               { b0 = (1 - c) / 2;   b1 = 1 - c;        b2 = (1 - c) / 2; }
		const double a0 = 1 + al;
		return { b0 / a0, b1 / a0, b2 / a0, -2 * c / a0, (1 - al) / a0 };
	}
	double run(double x)
	{
		const double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
};

uint32_t gRng = 0x9e3779b9u;
double noise()
{
	gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5;
	return static_cast<double>(gRng) / 4294967295.0 * 2.0 - 1.0;
}

void addKick(std::vector<float>& a, size_t at)
{
	double phase = 0;
	for (int i = 0; i < static_cast<int>(0.4 * kSr) && at + i < a.size(); ++i)
	{
		const double t = i / kSr;
		const double f = 45.0 + 75.0 * std::exp(-t / 0.04);
		phase += 2 * M_PI * f / kSr;
		a[at + i] += static_cast<float>(0.8 * std::sin(phase) * std::exp(-t / 0.2));
	}
}

void addSnare(std::vector<float>& a, size_t at)
{
	// noise kept out of the hat band (10 kHz+) so the classes are separable
	Biquad bp = Biquad::make(false, true, 2000.0, 0.7);
	Biquad l1 = Biquad::make(false, false, 6000.0, 0.7), l2 = l1;
	for (int i = 0; i < static_cast<int>(0.3 * kSr) && at + i < a.size(); ++i)
	{
		const double t = i / kSr;
		const double tone = 0.3 * std::sin(2 * M_PI * 200.0 * t) * std::exp(-t / 0.05);
		const double n = l2.run(l1.run(bp.run(noise())));
		a[at + i] += static_cast<float>(tone + 1.2 * n * std::exp(-t / 0.1));
	}
}

void addHat(std::vector<float>& a, size_t at)
{
	Biquad h1 = Biquad::make(true, false, 9000.0, 0.7), h2 = h1;
	for (int i = 0; i < static_cast<int>(0.15 * kSr) && at + i < a.size(); ++i)
	{
		const double t = i / kSr;
		a[at + i] += static_cast<float>(0.4 * h2.run(h1.run(noise())) * std::exp(-t / 0.03));
	}
}

struct Pattern
{
	std::vector<float>  audio;
	std::vector<double> hits[3];  // kick, snare, hat (seconds)
};

Pattern makePattern(double seconds, double bpm)
{
	Pattern p;
	p.audio.assign(static_cast<size_t>(seconds * kSr), 0.0f);
	const double beat = 60.0 / bpm;
	for (int b = 0; (b + 1) * beat < seconds - 0.5; ++b)
	{
		const double t = 0.25 + b * beat;
		addKick(p.audio, static_cast<size_t>(t * kSr));
		p.hits[0].push_back(t);
		if (b % 2 == 1) { addSnare(p.audio, static_cast<size_t>(t * kSr)); p.hits[1].push_back(t); }
		const double off = t + beat / 2;
		addHat(p.audio, static_cast<size_t>(off * kSr));
		p.hits[2].push_back(off);
	}
	return p;
}

constexpr double kScoreFrom = 0.6;  // s, see header

/// greedy one-to-one match within +/-tol over t >= kScoreFrom; returns {precision, recall}
std::pair<double, double> match(const std::vector<double>& refAll, const std::vector<double>& estAll, double tol)
{
	std::vector<double> ref, est;
	for (double r : refAll) if (r >= kScoreFrom) ref.push_back(r);
	for (double e : estAll) if (e >= kScoreFrom - tol) est.push_back(e);
	std::vector<bool> used(est.size(), false);
	int tp = 0;
	for (double r : ref)
		for (size_t j = 0; j < est.size(); ++j)
			if (!used[j] && std::fabs(est[j] - r) <= tol) { used[j] = true; ++tp; break; }
	return { est.empty() ? 0.0 : static_cast<double>(tp) / est.size(),
	         ref.empty() ? 0.0 : static_cast<double>(tp) / ref.size() };
}

void setParams(FakeInputs& in, bool drums)
{
	in.str["Mode"]             = "realtime";
	in.str["Onsetmethod"]      = "complex";
	in.str["Rtwindowsize"]     = "1024";
	in.str["Windowtype"]       = "blackmanharris62";
	in.dbl["Onsetsensitivity"] = 0.5;
	in.integer["Bpmmin"]       = 60;
	in.integer["Bpmmax"]       = 180;
	in.integer["Enabledrums"]  = drums ? 1 : 0;
	in.str["Drumwindow"]       = "2048";
	in.dbl["Kicksensitivity"]  = 0.5;
	in.dbl["Percsensitivity"]  = 0.5;
	in.dbl["Hatsensitivity"]   = 0.5;
}

const char* kNames[3] = { "kick", "perc", "hat" };

} // namespace

int main()
{
	const Pattern pat = makePattern(10.0, 110.0);
	std::vector<float> withSilence = pat.audio;
	withSilence.resize(withSilence.size() + static_cast<size_t>(3.0 * kSr), 0.0f);

	// ---- realtime: drums on vs off over identical audio ----
	std::vector<std::vector<float>> on, off;
	{
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, true);
		driveRealtime(chop, in, withSilence, kSlice, kSr, 0, &on);
	}
	{
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, false);
		driveRealtime(chop, in, withSilence, kSlice, kSr, 0, &off);
	}

	check("realtime: 12 channels with drums on", !on.empty() && on.back().size() == 12,
	      on.empty() ? 0 : static_cast<double>(on.back().size()), 12);
	check("realtime: 6 channels with drums off", !off.empty() && off.back().size() == 6,
	      off.empty() ? 0 : static_cast<double>(off.back().size()), 6);

	bool identical = on.size() == off.size();
	for (size_t c = 0; identical && c < on.size(); ++c)
		for (int ch = 0; ch < kRhythm; ++ch)
			if (on[c].size() < 12 || on[c][ch] != off[c][ch]) { identical = false; break; }
	check("realtime: rhythm channels bit-identical, drums on", identical, identical, 1);

	const double songEnd = pat.audio.size() / kSr;
	bool levelsInRange = true;
	int silentTriggers = 0;
	std::vector<double> rtHits[3];
	for (size_t c = 0; c < on.size() && on[c].size() >= 12; ++c)
	{
		const double t = (c + 1) * kSlice / kSr;
		for (int k = 0; k < 3; ++k)
		{
			if (on[c][kTrig + k] > 0.5f)
			{
				if (t > songEnd + 0.5) ++silentTriggers;
				else rtHits[k].push_back(t);
			}
			const float lv = on[c][kLevel + k];
			if (!(lv >= 0.0f && lv <= 1.0f)) levelsInRange = false;
		}
	}
	// unmatched detections: print the nearest labelled hit of any class
	for (int k = 0; k < 3; ++k)
		for (double t : rtHits[k])
		{
			double best = 9.0; int bestCls = -1;
			for (int o = 0; o < 3; ++o)
				for (double r : pat.hits[o])
					if (std::fabs(r - t) < std::fabs(best)) { best = t - r; bestCls = o; }
			bool own = false;
			for (double r : pat.hits[k]) own |= std::fabs(r - t) <= 0.05;
			if (!own)
				std::printf("  extra %-5s at %.3f s: nearest %s %+.0f ms\n", kNames[k], t,
				            bestCls >= 0 ? kNames[bestCls] : "-", best * 1000);
		}

	for (int k = 0; k < 3; ++k)
	{
		const auto pr = match(pat.hits[k], rtHits[k], 0.05);
		char label[64];
		std::snprintf(label, sizeof label, "realtime %s precision", kNames[k]);
		check(label, pr.first >= 0.9, pr.first, 0.9);
		std::snprintf(label, sizeof label, "realtime %s recall", kNames[k]);
		check(label, pr.second >= 0.9, pr.second, 0.9);
	}
	check("realtime: levels within 0-1", levelsInRange, levelsInRange, 1);
	check("realtime: no triggers in digital silence", silentTriggers == 0, silentTriggers, 0);

	// ---- realtime: near-silent lead-in (noise ~-90 dBFS peak) before any hit ----
	// every running peak is still 0 here, so only the absolute floor stops a trigger
	{
		std::vector<float> leadIn(static_cast<size_t>(1.0 * kSr));
		for (float& s : leadIn) s = static_cast<float>(3e-5 * noise());
		leadIn.insert(leadIn.end(), pat.audio.begin(), pat.audio.end());

		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, true);
		std::vector<std::vector<float>> tr;
		driveRealtime(chop, in, leadIn, kSlice, kSr, 0, &tr);
		int leadTriggers = 0;
		for (size_t c = 0; c < tr.size(); ++c)
			if ((c + 1) * kSlice / kSr < 0.95 && tr[c].size() >= 12)
				for (int k = 0; k < 3; ++k)
					if (tr[c][kTrig + k] > 0.5f) ++leadTriggers;
		check("realtime: no triggers on a near-silent lead-in", leadTriggers == 0, leadTriggers, 0);
	}

	// ---- realtime: the 1024 table ----
	{
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, true);
		in.str["Drumwindow"] = "1024";
		std::vector<std::vector<float>> tr;
		driveRealtime(chop, in, pat.audio, kSlice, kSr, 0, &tr);
		for (int k = 0; k < 3; ++k)
		{
			std::vector<double> hits;
			for (size_t c = 0; c < tr.size(); ++c)
				if (tr[c].size() >= 12 && tr[c][kTrig + k] > 0.5f) hits.push_back((c + 1) * kSlice / kSr);
			const auto pr = match(pat.hits[k], hits, 0.05);
			char label[64];
			std::snprintf(label, sizeof label, "realtime 1024 %s F", kNames[k]);
			const double f = (pr.first + pr.second > 0) ? 2 * pr.first * pr.second / (pr.first + pr.second) : 0;
			check(label, f >= 0.9, f, 0.9);
		}
	}

	// ---- realtime: drums keep running while the onset window is still filling ----
	{
		std::vector<float> kickAtZero(static_cast<size_t>(kSr), 0.0f);
		addKick(kickAtZero, 0);
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, true);
		in.str["Rtwindowsize"] = "4096";  // onset needs 6 cooks, drums at 1024 need 2
		in.str["Drumwindow"]   = "1024";
		std::vector<std::vector<float>> tr;
		driveRealtime(chop, in, kickAtZero, kSlice, kSr, 0, &tr);
		bool live = tr.size() > 4;
		for (size_t c = 2; live && c <= 4; ++c)
			live = tr[c].size() >= 12 && tr[c][kLevel] > 0.0f;
		check("realtime: low level live before the onset window fills", live, live, 1);
	}

	// ---- realtime: a band above Nyquist is reported ----
	{
		const double lowSr = 16000.0;
		std::vector<float> tone(static_cast<size_t>(lowSr), 0.0f);
		for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<float>(0.3 * std::sin(2 * M_PI * 100.0 * i / lowSr));
		EssentiaRhythmCHOP chop(nullptr);
		FakeInputs in;
		setParams(in, true);
		const auto r = driveRealtime(chop, in, tone, 267, lowSr);
		const bool warned = r.warning.find("hat band no bins") != std::string::npos;
		check("realtime 16 kHz: warns that the hat band is empty", warned, warned, 1);
	}

	// ---- batch ----
	{
		EssentiaRhythmCHOP chop(nullptr);  // registers the algorithms
		EssentiaRhythmCHOP::BatchRhythmParams p{};
		p.fftSize = 2048; p.hopSize = 512; p.windowType = "blackmanharris62"; p.zeroPad = 0;
		p.rhythmMethod = 1; p.onsetMethodIdx = 1; p.sensitivity = 0.5f; p.bpmMin = 60; p.bpmMax = 180;
		p.drums = true; p.drumWindow = 2048; p.kickSens = p.percSens = p.hatSens = 0.5f;

		EssentiaTD::AudioSnapshot snap;
		snap.data = pat.audio;
		snap.numSamples = static_cast<int>(pat.audio.size());
		snap.sampleRate = kSr;
		std::atomic<bool>  cancel{false};
		std::atomic<float> progress{0.0f};
		EssentiaTD::AsyncBatchResult r = EssentiaRhythmCHOP::computeBatchAsync(snap, p, cancel, progress);

		EssentiaRhythmCHOP::BatchRhythmParams pOff = p;
		pOff.drums = false;
		EssentiaTD::AsyncBatchResult rOff = EssentiaRhythmCHOP::computeBatchAsync(snap, pOff, cancel, progress);
		bool batchIdentical = r.success && rOff.success && rOff.cache.size() == 6 && r.cache.size() >= 6;
		for (int ch = 0; batchIdentical && ch < kRhythm; ++ch)
			batchIdentical = r.cache[ch] == rOff.cache[ch];
		check("batch: rhythm channels bit-identical, drums on", batchIdentical, batchIdentical, 1);

		check("batch: succeeded with 12 channels", r.success && r.cache.size() == 12,
		      static_cast<double>(r.cache.size()), 12);
		if (r.success && r.cache.size() == 12)
		{
			for (int k = 0; k < 3; ++k)
			{
				std::vector<double> hits;
				for (int f = 0; f < r.numFrames; ++f)
					if (r.cache[kTrig + k][f] > 0.5f) hits.push_back(f * 512.0 / kSr);
				const auto pr = match(pat.hits[k], hits, 0.05);
				char label[64];
				std::snprintf(label, sizeof label, "batch %s precision", kNames[k]);
				check(label, pr.first >= 0.9, pr.first, 0.9);
				std::snprintf(label, sizeof label, "batch %s recall", kNames[k]);
				check(label, pr.second >= 0.9, pr.second, 0.9);
			}
		}

		// drum timing must not follow FFT Size: frame f's drum window is fixed by hop and Drum Window
		EssentiaRhythmCHOP::BatchRhythmParams pBig = p;
		pBig.fftSize = 4096;
		EssentiaTD::AsyncBatchResult rBig = EssentiaRhythmCHOP::computeBatchAsync(snap, pBig, cancel, progress);
		bool fftIndependent = r.success && rBig.success && r.cache.size() == 12 && rBig.cache.size() == 12;
		const int common = std::min(r.numFrames, rBig.numFrames);
		for (int ch = kRhythm; fftIndependent && ch < 12; ++ch)
			fftIndependent = std::equal(r.cache[ch].begin(), r.cache[ch].begin() + common, rBig.cache[ch].begin());
		check("batch: drum channels independent of FFT Size", fftIndependent, fftIndependent, 1);
	}

	std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED",
	            gFailures, gFailures == 1 ? "" : "s");
	return gFailures ? 1 : 0;
}
