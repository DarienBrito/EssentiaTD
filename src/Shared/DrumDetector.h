#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DrumDetector: kick / perc / hat triggers and 0-1 band levels from one
// magnitude spectrum per frame (blackmanharris62, Essentia-normalized).
// Per class: band log-flux, floor relative to its running peak (perc also
// above a multiple of its recent mean), band-level gate, 50 ms refractory.
// Constants tuned per window on a drum corpus (diag/drum-detect, scored on a
// held-out half). TD-type-free.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace EssentiaTD
{

class DrumDetector
{
public:
	static constexpr int kNumClasses = 3;  // kick, perc, hat

	struct ClassConfig
	{
		float lo, hi;     // band edges, Hz
		float gamma;      // log1p(gamma * mag) compression knee
		float peakDecay;  // running-peak decay per 1/60 s
		float floorRel;   // trigger floor as a fraction of the running peak at sensitivity 0.5
		float localLam;   // also above localLam x mean odf of the last kLocalSeconds (0 = off), at sensitivity 0.5
	};

	/// window must be 1024 or 2048 (anything else uses the 2048 table)
	void configure(int window, double sampleRate)
	{
		static const ClassConfig k1024[kNumClasses] = {
			{   30.0f,   150.0f,    1.0f, 0.999f, 0.6f, 0.0f },
			{  200.0f,  2500.0f,    1.0f, 0.995f, 0.6f, 2.0f },
			{ 10000.0f, 20000.0f, 1000.0f, 0.999f, 0.3f, 0.0f },
		};
		static const ClassConfig k2048[kNumClasses] = {
			{   30.0f,   150.0f,    1.0f, 0.999f, 0.6f, 0.0f },
			{  200.0f,  2500.0f,    1.0f, 0.995f, 0.7f, 2.0f },
			{ 10000.0f, 20000.0f, 1000.0f, 0.995f, 0.4f, 0.0f },
		};
		const ClassConfig* table = (window == 1024) ? k1024 : k2048;

		myWindow     = window;
		mySampleRate = sampleRate;
		const int bins = window / 2 + 1;
		myRefractorySamples = static_cast<int64_t>(std::llround(kRefractorySeconds * sampleRate));

		for (int c = 0; c < kNumClasses; ++c)
		{
			State& s = myState[c];
			s.cfg = table[c];
			// bins with lo <= f < hi, f = k * sr / window
			s.k0 = std::min(bins, static_cast<int>(std::ceil(s.cfg.lo * window / sampleRate)));
			s.k1 = std::min(bins, static_cast<int>(std::ceil(s.cfg.hi * window / sampleRate)));
			s.prevComp.assign(static_cast<size_t>(std::max(0, s.k1 - s.k0)), 0.0f);
		}
		reset();
	}

	void reset()
	{
		for (State& s : myState)
		{
			std::fill(s.prevComp.begin(), s.prevComp.end(), 0.0f);
			s.prevOdf   = 0.0;
			s.peak      = 0.0;
			s.levelPeak = 0.0;
			s.lastFire  = INT64_MIN / 2;
			s.trigger   = false;
			s.level     = 0.0f;
			s.histCount = 0;
			s.histPos   = 0;
		}
		myFrames     = 0;
		mySampleTime = 0;
	}

	/// After a gap in the audio: the next frame computes no flux (it has no
	/// neighbour), but running peaks and refractory stay, so the first sound
	/// after the gap is not judged as a cold start.
	void restart()
	{
		for (State& s : myState)
		{
			std::fill(s.prevComp.begin(), s.prevComp.end(), 0.0f);
			s.prevOdf   = 0.0;
			s.trigger   = false;
			s.histCount = 0;
			s.histPos   = 0;
		}
		myFrames = 0;
	}

	/// Non-empty when a class's band is empty or cut by Nyquist at this sample rate / window.
	std::string bandWarning() const
	{
		static const char* const names[kNumClasses] = { "kick", "perc", "hat" };
		std::string out;
		for (int c = 0; c < kNumClasses; ++c)
		{
			const State& s = myState[c];
			const char* what = (s.k1 <= s.k0) ? "no bins (never fires)"
			                 : (s.cfg.hi > mySampleRate / 2.0) ? "cut by Nyquist" : nullptr;
			if (!what) continue;
			out += (out.empty() ? "Drums: " : ", ");
			out += std::string(names[c]) + " band " + what;
		}
		return out;
	}

	int window() const { return myWindow; }
	double sampleRate() const { return mySampleRate; }

	/// One frame. mag = window/2+1 magnitudes; samplesAdvanced = audio since the
	/// previous frame (cook timeslice in realtime, hop in batch); sens[c] in 0..1.
	void process(const std::vector<float>& mag, int64_t samplesAdvanced, const float sens[kNumClasses])
	{
		mySampleTime += samplesAdvanced;
		const double steps60 = static_cast<double>(samplesAdvanced) / mySampleRate * 60.0;

		for (int c = 0; c < kNumClasses; ++c)
		{
			State& s = myState[c];
			s.trigger = false;

			double odf = 0.0, energy = 0.0;
			for (int k = s.k0; k < s.k1 && k < static_cast<int>(mag.size()); ++k)
			{
				const float m    = mag[static_cast<size_t>(k)];
				const float comp = std::log1p(s.cfg.gamma * m);
				float& prev      = s.prevComp[static_cast<size_t>(k - s.k0)];
				if (myFrames > 0)
					odf += std::max(0.0f, comp - prev);
				prev = comp;
				energy += static_cast<double>(m) * m;
			}

			s.peak = std::max(odf, s.peak * std::pow(static_cast<double>(s.cfg.peakDecay), steps60));
			const double e = std::sqrt(energy);

			if (myFrames > 0)
			{
				// sensitivity 0.5 = tuned floor, 1 = floor 0, 0 = floor 1 (only a new loudest hit)
				const double sv  = std::clamp(static_cast<double>(sens[c]), 0.0, 1.0);
				const double rel = sv >= 0.5 ? s.cfg.floorRel * (1.0 - sv) * 2.0
				                             : s.cfg.floorRel + (1.0 - s.cfg.floorRel) * (0.5 - sv) * 2.0;
				double floor = rel * s.peak;
				if (s.cfg.localLam > 0.0f && s.histCount > 0)
				{
					// mean of the previous n odf values (current excluded), n = kLocalSeconds of frames;
					// sensitivity scales it like the peak floor (exact at 0.5, off at 1)
					const int n = std::clamp(static_cast<int>(std::lround(kLocalSeconds * mySampleRate /
					                         static_cast<double>(std::max<int64_t>(1, samplesAdvanced)))), 1, kHistSize);
					const int cnt = std::min(n, s.histCount);
					double sum = 0.0;
					for (int i = cnt; i >= 1; --i)
						sum += s.hist[static_cast<size_t>((s.histPos - i + kHistSize) % kHistSize)];
					floor = std::max(floor, s.cfg.localLam * (rel / s.cfg.floorRel) * (sum / cnt));
				}
				const bool aboveFloor = e >= kLevelGate && odf >= floor;
				const bool refractory = mySampleTime - s.lastFire < myRefractorySamples;
				// still inside the previous frame's hit: neither rising nor newly above the floor
				const bool sameHit = odf <= s.prevOdf && s.prevOdf >= floor;
				if (aboveFloor && !refractory && !sameHit)
				{
					s.trigger  = true;
					s.lastFire = mySampleTime;
				}
			}
			s.prevOdf = odf;
			s.hist[static_cast<size_t>(s.histPos)] = odf;
			s.histPos   = (s.histPos + 1) % kHistSize;
			s.histCount = std::min(s.histCount + 1, kHistSize);

			s.levelPeak = std::max(e, s.levelPeak * std::pow(kLevelDecay, steps60));
			s.level = (s.levelPeak > kLevelFloor) ? static_cast<float>(e / s.levelPeak) : 0.0f;
		}
		++myFrames;
	}

	bool  trigger(int c) const { return myState[c].trigger; }
	float level(int c)   const { return myState[c].level; }

	/// Batch: output frame f sits at time f * hop. The detector steps at its own
	/// hop, min(hop, kMaxBatchHop), so a large Hop Size never thins the analysis;
	/// each step lands in its nearest output frame (triggers OR, levels max).
	/// A step at time t reads the window ending at t + endOffset (half the drum
	/// window centres it on t). Steps whose window starts before the audio are skipped.
	/// Returns the number of steps analyzed (0 = input shorter than the window).
	template <typename SpectrumFn>
	int processBatch(int numFrames, int hop, int endOffset, const float sens[kNumClasses],
	                 SpectrumFn&& spectrumEndingAt,
	                 std::vector<std::vector<float>>& outTriggers,
	                 std::vector<std::vector<float>>& outLevels)
	{
		reset();
		outTriggers.assign(kNumClasses, std::vector<float>(static_cast<size_t>(std::max(0, numFrames)), 0.0f));
		outLevels.assign(kNumClasses, std::vector<float>(static_cast<size_t>(std::max(0, numFrames)), 0.0f));
		if (numFrames <= 0 || hop <= 0) return 0;
		int steps = 0;

		const int64_t H    = hop;
		const int64_t step = std::min<int64_t>(H, kMaxBatchHop);
		const int64_t tMax = static_cast<int64_t>(numFrames) * H - H / 2;  // nearest frame stays < numFrames
		bool started = false;
		for (int64_t t = 0; t < tMax; t += step)
		{
			const int64_t end = t + endOffset;
			if (end < myWindow) continue;
			const std::vector<float>* mag = spectrumEndingAt(end);
			if (!mag) continue;
			process(*mag, started ? step : 0, sens);
			started = true;
			++steps;
			const size_t f = static_cast<size_t>((t + H / 2) / H);
			for (int c = 0; c < kNumClasses; ++c)
			{
				if (trigger(c)) outTriggers[c][f] = 1.0f;
				outLevels[c][f] = std::max(outLevels[c][f], level(c));
			}
		}
		return steps;
	}

	/// Largest batch detector step in samples: the constants were tuned at hop 512.
	static constexpr int kMaxBatchHop = 512;

private:
	static constexpr double kRefractorySeconds = 0.05;
	static constexpr double kLevelGate         = 1e-4;   // band energy (~-80 dBFS) below which nothing triggers, even before any peak exists
	static constexpr double kLevelDecay        = 0.999;  // level running peak, per 1/60 s (~17 s)
	static constexpr double kLevelFloor        = 1e-9;
	static constexpr double kLocalSeconds      = 0.1;    // local-mean window for localLam
	static constexpr int    kHistSize          = 64;     // odf history; caps that window in frames

	struct State
	{
		ClassConfig cfg{};
		int k0 = 0, k1 = 0;
		std::vector<float> prevComp;
		double  prevOdf   = 0.0;
		double  peak      = 0.0;
		double  levelPeak = 0.0;
		int64_t lastFire  = 0;
		bool    trigger   = false;
		float   level     = 0.0f;
		std::array<double, kHistSize> hist{};  // past odf values, ring
		int     histPos   = 0;
		int     histCount = 0;
	};

	State   myState[kNumClasses];
	int     myWindow     = 0;
	double  mySampleRate = 44100.0;
	int64_t myRefractorySamples = 0;
	int64_t myFrames     = 0;
	int64_t mySampleTime = 0;
};

} // namespace EssentiaTD
