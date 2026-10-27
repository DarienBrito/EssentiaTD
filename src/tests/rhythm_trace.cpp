// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rhythm trace tool (not a ctest): drives the shipped realtime path over a raw
// float32 mono file and writes every cook's channel values as CSV. Used for
// bit-identity checks across code changes and C++ vs Python parity.
// usage: rhythm_trace <in.f32> <sampleRate> <timeslice|batch> <out.csv> [Par=value ...]
// "batch" runs computeBatchAsync at the batch defaults (2048/512, complex,
// degara) and writes one row per batch frame. Build with TRACE_NO_DRUMS
// against a tree whose BatchRhythmParams predates the drum fields.

#include "TDStubs.h"
#include "EssentiaRhythmCHOP.h"
#include "Shared/EssentiaInit.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using EssentiaTD::EssentiaRhythmCHOP;
using EssentiaTDTest::FakeInputs;
using EssentiaTDTest::driveRealtime;

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		std::fprintf(stderr, "usage: rhythm_trace <in.f32> <sr> <timeslice> <out.csv> [Par=value ...]\n");
		return 2;
	}
	const double sampleRate = std::atof(argv[2]);
	const int    timeslice  = std::atoi(argv[3]);

	std::FILE* f = std::fopen(argv[1], "rb");
	if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
	std::vector<float> audio;
	float buf[4096];
	size_t n;
	while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0)
		audio.insert(audio.end(), buf, buf + n);
	std::fclose(f);

	FakeInputs in;
	in.str["Mode"]             = "realtime";
	in.str["Onsetmethod"]      = "complex";
	in.str["Rtwindowsize"]     = "1024";
	in.str["Windowtype"]       = "blackmanharris62";
	in.dbl["Onsetsensitivity"] = 0.5;
	in.integer["Bpmmin"]       = 60;
	in.integer["Bpmmax"]       = 180;
	// TD defaults for the drum page; FakeInputs answers 0 for anything unset
	in.str["Drumwindow"]       = "2048";
	in.dbl["Kicksensitivity"]  = 0.5;
	in.dbl["Percsensitivity"]  = 0.5;
	in.dbl["Hatsensitivity"]   = 0.5;

	// Par=value overrides: numeric values go to dbl and integer, others to str
	for (int i = 5; i < argc; ++i)
	{
		const std::string kv = argv[i];
		const size_t eq = kv.find('=');
		if (eq == std::string::npos) continue;
		const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
		char* end = nullptr;
		const double d = std::strtod(v.c_str(), &end);
		if (end && *end == '\0' && !v.empty())
		{
			in.dbl[k]     = d;
			in.integer[k] = static_cast<int>(d);
		}
		in.str[k] = v;
	}

	std::vector<std::vector<float>> trace;
	if (std::string(argv[3]) == "batch")
	{
		// no CHOP is constructed here, so nothing else registers the algorithms
		std::string initErr;
		if (!EssentiaTD::ensureEssentiaInit(initErr))
		{
			std::fprintf(stderr, "essentia init failed: %s\n", initErr.c_str());
			return 1;
		}
		EssentiaRhythmCHOP::BatchRhythmParams p{};
		p.fftSize        = 2048;
		p.hopSize        = 512;
		p.windowType     = "blackmanharris62";
		p.zeroPad        = 0;
		p.rhythmMethod   = 1;  // degara
		p.onsetMethodIdx = 1;  // complex
		p.sensitivity    = 0.5f;
		p.bpmMin         = 60;
		p.bpmMax         = 180;
#ifndef TRACE_NO_DRUMS
		p.drums      = in.integer["Enabledrums"] != 0;
		p.drumWindow = in.str["Drumwindow"] == "1024" ? 1024 : 2048;
		p.kickSens   = in.dbl.count("Kicksensitivity") ? static_cast<float>(in.dbl["Kicksensitivity"]) : 0.5f;
		p.percSens   = in.dbl.count("Percsensitivity") ? static_cast<float>(in.dbl["Percsensitivity"]) : 0.5f;
		p.hatSens    = in.dbl.count("Hatsensitivity") ? static_cast<float>(in.dbl["Hatsensitivity"]) : 0.5f;
#endif
		EssentiaTD::AudioSnapshot snap;
		snap.data       = audio;
		snap.numSamples = static_cast<int>(audio.size());
		snap.sampleRate = sampleRate;
		std::atomic<bool>  cancel{false};
		std::atomic<float> progress{0.0f};
		EssentiaTD::AsyncBatchResult r = EssentiaRhythmCHOP::computeBatchAsync(snap, p, cancel, progress);
		if (!r.success) { std::fprintf(stderr, "batch failed: %s\n", r.error.c_str()); return 1; }
		for (int f = 0; f < r.numFrames; ++f)
		{
			std::vector<float> row;
			for (const auto& ch : r.cache) row.push_back(ch[static_cast<size_t>(f)]);
			trace.push_back(row);
		}
	}
	else
	{
		EssentiaRhythmCHOP chop(nullptr);
		driveRealtime(chop, in, audio, timeslice, sampleRate, 0, &trace);
	}

	std::FILE* o = std::fopen(argv[4], "w");
	if (!o) { std::fprintf(stderr, "cannot write %s\n", argv[4]); return 2; }
	for (const auto& row : trace)
	{
		for (size_t c = 0; c < row.size(); ++c)
			std::fprintf(o, c ? ",%.9g" : "%.9g", row[c]);
		std::fprintf(o, "\n");
	}
	std::fclose(o);
	std::printf("%zu cooks, %zu channels -> %s\n", trace.size(),
	            trace.empty() ? 0 : trace.back().size(), argv[4]);
	return 0;
}
