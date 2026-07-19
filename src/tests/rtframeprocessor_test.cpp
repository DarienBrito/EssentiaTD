// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Golden-vector test for Shared/RTFrameProcessor.h — the RT frame machinery
// every analyzer's internal FFT rides on. Headless: no TouchDesigner types.
//
// Feeds a 440 Hz sine at 48 kHz in 800-sample timeslices (the shape TD
// delivers at 60 fps) and asserts the analyzed spectrum: peak bin, energy
// concentration, spectral centroid. Also exercises the accumulation
// contract: not-ready gating, the totalCooks double-write guard, reset,
// and zero-padded configuration.

#include "../Shared/RTFrameProcessor.h"
#include "../Shared/EssentiaInit.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

int gFails = 0;

void check(bool cond, const char* msg)
{
	if (cond)
		std::printf("  ok    %s\n", msg);
	else
	{
		std::printf("  FAIL  %s\n", msg);
		++gFails;
	}
}

int argmax(const std::vector<essentia::Real>& v)
{
	int best = 0;
	for (int i = 1; i < (int)v.size(); ++i)
		if (v[i] > v[best]) best = i;
	return best;
}

} // namespace

int main()
{
	std::string initErr;
	if (!EssentiaTD::ensureEssentiaInit(initErr))
	{
		std::printf("FAIL essentia init: %s\n", initErr.c_str());
		return 1;
	}

	const double kSampleRate = 48000.0;
	const double kFreq = 440.0;
	const size_t kSlice = 800;   // ~one 60 fps timeslice at 48 kHz

	EssentiaTD::RTFrameProcessor proc;
	proc.configure(1024, "hann", 0);

	size_t phase = 0;
	int64_t cook = 1;
	std::vector<float> slice(kSlice);
	auto feed = [&]()
	{
		for (size_t i = 0; i < kSlice; ++i, ++phase)
			slice[i] = (float)std::sin(2.0 * M_PI * kFreq * (double)phase / kSampleRate);
		proc.accumulate(slice.data(), kSlice, cook++);
	};

	std::printf("accumulation contract:\n");
	feed();
	check(!proc.frameReady(), "not ready after 800/1024 samples");
	check(!proc.processLatest(), "processLatest refuses a partial window");
	check(proc.accumulatingWarning().find("800/1024") != std::string::npos,
	      "accumulating warning reports 800/1024");

	// Same cook stamp again must be a no-op (forced re-cook of the consumer)
	proc.accumulate(slice.data(), kSlice, cook - 1);
	check(!proc.frameReady(), "double-write guard: same stamp adds nothing");

	feed();
	check(proc.frameReady(), "ready after 1600 samples");
	check(proc.processLatest(), "processLatest analyzes a full window");

	std::printf("spectrum golden values (1024/hann/no pad):\n");
	{
		const auto& mag = proc.magnitude();
		check((int)mag.size() == 513, "specBins == 513");
		check((int)proc.phase().size() == 513, "phase bins == 513");

		// 440 Hz at 46.875 Hz/bin -> bin 9.387; hann main lobe peaks at 9
		const int peak = argmax(mag);
		check(peak == 9 || peak == 10, "peak bin at 9-10 (440 Hz)");

		double num = 0.0, den = 0.0, local = 0.0;
		for (int i = 0; i < (int)mag.size(); ++i)
		{
			const double f = i * kSampleRate / 1024.0;
			num += f * mag[i];
			den += mag[i];
			if (i >= 7 && i <= 12) local += mag[i];
		}
		const double centroid = num / den;
		std::printf("        centroid %.1f Hz, peak bin %d, concentration %.4f\n",
		            centroid, peak, local / den);
		check(std::fabs(centroid - kFreq) < 40.0, "centroid within 40 Hz of 440");
		check(local / den > 0.9, "energy concentrated in bins 7-12");
	}

	std::printf("reset:\n");
	proc.reset();
	check(!proc.frameReady(), "reset drops accumulated audio");

	std::printf("zero-padded configuration (1024 + 1024 pad):\n");
	{
		proc.configure(1024, "hann", 1024);
		// configure resets the stamp, so earlier cook values are fine
		feed();
		feed();
		check(proc.frameReady(), "ready (ring still holds one true window)");
		check(proc.processLatest(), "processLatest analyzes padded window");
		check((int)proc.magnitude().size() == 1025, "padded specBins == 1025");

		// 440 Hz at 23.4375 Hz/bin -> bin 18.77; expect peak at 18-19
		const int peak = argmax(proc.magnitude());
		check(peak == 18 || peak == 19, "padded peak bin at 18-19 (440 Hz)");
	}

	if (gFails == 0)
	{
		std::printf("PASS (all checks)\n");
		return 0;
	}
	std::printf("FAIL (%d checks)\n", gFails);
	return 1;
}
