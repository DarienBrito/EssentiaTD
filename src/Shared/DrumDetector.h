#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DrumDetector: kick / snare / hat triggers and 0-1 band levels from one
// magnitude spectrum per frame (blackmanharris62, Essentia-normalized).
// Per class: band log-flux, floor relative to its running peak, band-level
// gate, 50 ms refractory. Constants tuned per window on a drum corpus
// (diag/drum-detect, scored on a held-out half). TD-type-free.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace EssentiaTD
{

class DrumDetector
{
public:
	static constexpr int kNumClasses = 3;  // kick, snare, hat

	struct ClassConfig
	{
		float lo, hi;     // band edges, Hz
		float gamma;      // log1p(gamma * mag) compression knee
		float peakDecay;  // running-peak decay per 1/60 s
		float floorRel;   // trigger floor as a fraction of the running peak at sensitivity 0.5
	};

	/// window must be 1024 or 2048 (anything else uses the 2048 table)
	void configure(int window, double sampleRate)
	{
		static const ClassConfig k1024[kNumClasses] = {
			{   30.0f,   150.0f,    1.0f, 0.999f, 0.6f },
			{  200.0f,  2500.0f,    1.0f, 0.995f, 0.6f },
			{ 10000.0f, 20000.0f, 1000.0f, 0.999f, 0.3f },
		};
		static const ClassConfig k2048[kNumClasses] = {
			{   30.0f,   150.0f,    1.0f, 0.999f, 0.6f },
			{  200.0f,  2500.0f,    1.0f, 0.995f, 0.7f },
			{ 10000.0f, 20000.0f, 1000.0f, 0.995f, 0.4f },
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
		}
		myFrames     = 0;
		mySampleTime = 0;
	}

	/// Non-empty when a class's band is empty or cut by Nyquist at this sample rate / window.
	std::string bandWarning() const
	{
		static const char* const names[kNumClasses] = { "kick", "snare", "hat" };
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
				const double floor = rel * s.peak;
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

			s.levelPeak = std::max(e, s.levelPeak * std::pow(kLevelDecay, steps60));
			s.level = (s.levelPeak > kLevelFloor) ? static_cast<float>(e / s.levelPeak) : 0.0f;
		}
		++myFrames;
	}

	bool  trigger(int c) const { return myState[c].trigger; }
	float level(int c)   const { return myState[c].level; }

	/// Batch: frame f's drum window ends at f * hop + endOffset. endOffset = half
	/// the drum window centres it on f * hop, which puts hits on their true time;
	/// it must not depend on the main FFT size. Frames whose window would start
	/// before the audio stay 0.
	template <typename SpectrumFn>
	void processBatch(int numFrames, int hop, int endOffset, const float sens[kNumClasses],
	                  SpectrumFn&& spectrumEndingAt,
	                  std::vector<std::vector<float>>& outTriggers,
	                  std::vector<std::vector<float>>& outLevels)
	{
		reset();
		outTriggers.assign(kNumClasses, std::vector<float>(static_cast<size_t>(numFrames), 0.0f));
		outLevels.assign(kNumClasses, std::vector<float>(static_cast<size_t>(numFrames), 0.0f));
		bool started = false;
		for (int f = 0; f < numFrames; ++f)
		{
			const int64_t end = static_cast<int64_t>(f) * hop + endOffset;
			if (end < myWindow) continue;
			const std::vector<float>* mag = spectrumEndingAt(end);
			if (!mag) continue;
			process(*mag, started ? hop : 0, sens);
			started = true;
			for (int c = 0; c < kNumClasses; ++c)
			{
				outTriggers[c][static_cast<size_t>(f)] = trigger(c) ? 1.0f : 0.0f;
				outLevels[c][static_cast<size_t>(f)]   = level(c);
			}
		}
	}

private:
	static constexpr double kRefractorySeconds = 0.05;
	static constexpr double kLevelGate         = 1e-4;   // band energy (~-80 dBFS) below which nothing triggers, even before any peak exists
	static constexpr double kLevelDecay        = 0.999;  // level running peak, per 1/60 s (~17 s)
	static constexpr double kLevelFloor        = 1e-9;

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
	};

	State   myState[kNumClasses];
	int     myWindow     = 0;
	double  mySampleRate = 44100.0;
	int64_t myRefractorySamples = 0;
	int64_t myFrames     = 0;
	int64_t mySampleTime = 0;
};

} // namespace EssentiaTD
