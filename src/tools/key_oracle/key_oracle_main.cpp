// SPDX-License-Identifier: AGPL-3.0-or-later
//
// key_oracle — offline key-detection oracle harness for EssentiaTD issue #12.
//
// Runs up to three chains over the same WAV file(s) and prints one JSON row
// per file (JSONL, byte-stable across runs):
//
//   plugin     — EssentiaTonalCHOP::computeBatchAsync(): the SHIPPED batch
//                code path, compiled straight from src/EssentiaTonalCHOP.cpp
//                into this executable. Zero re-implementation, zero drift.
//   oracle     — essentia standard::KeyExtractor with its own defaults
//                (compiled from _essentia_src keyextractor.cpp and registered
//                locally, because the pinned static lib excludes
//                algorithms/extractor/). Only sampleRate is overridden to the
//                file's true rate — KeyExtractor's 44100 default on a 48k file
//                would be a measurement bug, not a default.
//   experiment — harness-local chain that reuses the shipped
//                Shared/BatchFrameProcessor.h for framing/FFT and mirrors the
//                plugin's create() argument lists, with toggles for the five
//                audited divergences D1..D5.
//
// SELF-CHECK (drift guard): with every toggle at its plugin setting, the
// experiment chain must reproduce the plugin chain bit-exactly (key + scale
// strings equal, strength float bit-equal). If it does not, the harness
// prints DRIFT to stderr and exits 2 — the experiment chain can never
// silently diverge from shipped code.
//
// Determinism: same binary + same input + same flags => byte-identical
// stdout. See DESIGN.md "Determinism" for what can break that.
//
// Exit codes: 0 = ok, 1 = usage / IO / compute error, 2 = self-check drift.

#include "EssentiaTonalCHOP.h"            // shipped: BatchTonalParams + computeBatchAsync
#include "Shared/AsyncBatchRunner.h"      // shipped: AudioSnapshot
#include "Shared/BatchCommon.h"           // shipped: zeroPadFromFactor
#include "Shared/BatchFrameProcessor.h"   // shipped: framing + windowing + FFT
#include "Shared/EssentiaInit.h"          // shipped: essentia::init wrapper
#include "wav_reader.h"

#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>            // essentia::version
#include "algorithms/extractor/keyextractor.h"   // oracle classes (source compiled into this exe)

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using essentia::Real;

// ===========================================================================
// Key encoding tables — MUST match Shared/Utils.h encodeKey() (0 = C .. 11 = B)
// ===========================================================================

static const char* kNotes[12] = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static int keyStrToIdx(const std::string& k)
{
	// Same table + flats handling as Shared/Utils.h encodeKey()
	static const char* flats[12] = {
		"", "Db", "", "Eb", "", "", "Gb", "", "Ab", "", "Bb", ""
	};
	for (int i = 0; i < 12; ++i)
	{
		if (k == kNotes[i]) return i;
		if (flats[i][0] && k == flats[i]) return i;
	}
	return -1;
}

// ===========================================================================
// Options
// ===========================================================================

// Profile order MUST match EssentiaTonalCHOP.cpp profileNames[] (:866-868, :1019-1021)
static const char* kProfiles[6] = {
	"bgate", "temperley", "krumhansl", "edma", "diatonic", "gomez"
};

struct Options
{
	// plugin-chain config (defaults = shipped batch defaults: Tonal 4096/2048,
	// Parameters_Tonal defaults for the rest)
	int         profileIdx     = 0;         // bgate
	int         fft            = 4096;
	int         hop            = 2048;
	std::string window         = "hann";
	int         zeroPad        = 0;
	int         keyMode        = 0;         // 0 = global, 1 = windowed
	int         keyWindow      = 8;
	int         hpcpSize       = 12;
	float       tuning         = 440.0f;
	float       peakThreshold  = 0.00001f;
	float       peakMaxFreq    = 3500.0f;
	int         harmonics      = 0;         // Hpcpharmonics default
	int         normalizedIdx  = 0;         // 0=unitMax 1=unitSum 2=none
	bool        nonLinear      = false;

	// oracle config (defaults = KeyExtractor's own declareParameters defaults)
	std::string oracleProfile  = "bgate";
	int         oracleFrame    = 4096;
	int         oracleHop      = 4096;
	std::string oracleWindow   = "hann";
	float       oracleTuning   = 440.0f;

	// experiment toggles (D1..D5)
	bool        exp            = false;
	bool        d1RefKey       = false;     // usePolyphony=false, useThreeChords=false
	bool        d2Whiten       = false;     // SpectralWhitening before HPCP
	int         d3Harmonics    = -1;        // -1 = keep plugin value
	std::string d3Normalized;               // "" = keep plugin value
	float       d4HpcpMax      = -1.0f;     // <0 = plugin hardcoded 3500
	float       d4HpcpMin      = -1.0f;     // <0 = plugin hardcoded 20
	float       d4PeakMin      = -1.0f;     // <0 = plugin hardcoded 20 (ref default 25)
	bool        d5Gate         = false;     // normalize-to-peak + 0.2 gate on mean HPCP

	bool        selfCheck      = true;
	std::string manifestPath;
	std::string outPath;                    // empty = stdout

	std::vector<std::string> files;
};

static void usage()
{
	std::fprintf(stderr,
"key_oracle [flags] file1.wav [file2.wav ...]\n"
"  plugin chain (shipped computeBatchAsync):\n"
"    --profile=bgate|temperley|krumhansl|edma|diatonic|gomez   (default bgate)\n"
"    --fft=N --hop=N --window=S --zeropad=N   (default 4096/2048/hann/0)\n"
"    --keymode=global|windowed --keywindow=N  (default global/8)\n"
"    --hpcpsize=N --tuning=F --peak-threshold=F --peak-maxfreq=F\n"
"    --harmonics=N --normalized=unitMax|unitSum|none --nonlinear=0|1\n"
"  oracle (standard::KeyExtractor, own defaults; sampleRate always = file rate):\n"
"    --oracle-profile=S --oracle-frame=N --oracle-hop=N --oracle-window=S\n"
"    --oracle-tuning=F\n"
"  experiment chain (only with --exp; global key mode only):\n"
"    --exp --d1-refkey --d2-whiten --d3-harmonics=N\n"
"    --d3-normalized=S --d4-hpcp-maxfreq=F --d4-hpcp-minfreq=F\n"
"    --d4-peak-minfreq=F --d5-gate\n"
"    --all-ref   (= every D toggle at the KeyExtractor reference setting)\n"
"  misc:\n"
"    --manifest=PATH  (diag/key-accuracy/manifest.json ground truth)\n"
"    --out=PATH       (default stdout)\n"
"    --no-selfcheck   (skip the drift guard; do not use for real readings)\n");
}

static bool parseArgs(int argc, char** argv, Options& o)
{
	auto val = [](const char* a, const char* name) -> const char* {
		size_t n = std::strlen(name);
		if (std::strncmp(a, name, n) == 0 && a[n] == '=') return a + n + 1;
		return nullptr;
	};

	for (int i = 1; i < argc; ++i)
	{
		const char* a = argv[i];
		const char* v = nullptr;
		if (a[0] != '-') { o.files.push_back(a); continue; }

		if      ((v = val(a, "--profile"))) {
			int idx = -1;
			for (int k = 0; k < 6; ++k) if (std::strcmp(v, kProfiles[k]) == 0) idx = k;
			if (idx < 0) { std::fprintf(stderr, "unknown profile %s\n", v); return false; }
			o.profileIdx = idx;
		}
		else if ((v = val(a, "--fft")))            o.fft = std::atoi(v);
		else if ((v = val(a, "--hop")))            o.hop = std::atoi(v);
		else if ((v = val(a, "--window")))         o.window = v;
		else if ((v = val(a, "--zeropad")))        o.zeroPad = std::atoi(v);
		else if ((v = val(a, "--keymode")))        o.keyMode = (std::strcmp(v, "windowed") == 0) ? 1 : 0;
		else if ((v = val(a, "--keywindow")))      o.keyWindow = std::atoi(v);
		else if ((v = val(a, "--hpcpsize")))       o.hpcpSize = std::atoi(v);
		else if ((v = val(a, "--tuning")))         o.tuning = (float)std::atof(v);
		else if ((v = val(a, "--peak-threshold"))) o.peakThreshold = (float)std::atof(v);
		else if ((v = val(a, "--peak-maxfreq")))   o.peakMaxFreq = (float)std::atof(v);
		else if ((v = val(a, "--harmonics")))      o.harmonics = std::atoi(v);
		else if ((v = val(a, "--normalized"))) {
			if      (std::strcmp(v, "unitMax") == 0) o.normalizedIdx = 0;
			else if (std::strcmp(v, "unitSum") == 0) o.normalizedIdx = 1;
			else if (std::strcmp(v, "none")    == 0) o.normalizedIdx = 2;
			else { std::fprintf(stderr, "bad --normalized %s\n", v); return false; }
		}
		else if ((v = val(a, "--nonlinear")))      o.nonLinear = std::atoi(v) != 0;
		else if ((v = val(a, "--oracle-profile"))) o.oracleProfile = v;
		else if ((v = val(a, "--oracle-frame")))   o.oracleFrame = std::atoi(v);
		else if ((v = val(a, "--oracle-hop")))     o.oracleHop = std::atoi(v);
		else if ((v = val(a, "--oracle-window")))  o.oracleWindow = v;
		else if ((v = val(a, "--oracle-tuning")))  o.oracleTuning = (float)std::atof(v);
		else if (std::strcmp(a, "--exp") == 0)         o.exp = true;
		else if (std::strcmp(a, "--d1-refkey") == 0)   { o.exp = true; o.d1RefKey = true; }
		else if (std::strcmp(a, "--d2-whiten") == 0)   { o.exp = true; o.d2Whiten = true; }
		else if ((v = val(a, "--d3-harmonics")))       { o.exp = true; o.d3Harmonics = std::atoi(v); }
		else if ((v = val(a, "--d3-normalized")))      { o.exp = true; o.d3Normalized = v; }
		else if ((v = val(a, "--d4-hpcp-maxfreq")))    { o.exp = true; o.d4HpcpMax = (float)std::atof(v); }
		else if ((v = val(a, "--d4-hpcp-minfreq")))    { o.exp = true; o.d4HpcpMin = (float)std::atof(v); }
		else if ((v = val(a, "--d4-peak-minfreq")))    { o.exp = true; o.d4PeakMin = (float)std::atof(v); }
		else if (std::strcmp(a, "--d5-gate") == 0)     { o.exp = true; o.d5Gate = true; }
		else if (std::strcmp(a, "--all-ref") == 0) {
			o.exp = true;
			o.d1RefKey = true; o.d2Whiten = true;
			o.d3Harmonics = 4; o.d3Normalized = "none";
			o.d4HpcpMin = 25.0f; o.d4PeakMin = 25.0f;   // keyextractor.h minFrequency default 25
			o.d5Gate = true;
		}
		else if ((v = val(a, "--manifest")))       o.manifestPath = v;
		else if ((v = val(a, "--out")))            o.outPath = v;
		else if (std::strcmp(a, "--no-selfcheck") == 0) o.selfCheck = false;
		else if (std::strcmp(a, "--help") == 0)    { usage(); return false; }
		else { std::fprintf(stderr, "unknown flag %s\n", a); usage(); return false; }
	}
	if (o.files.empty()) { usage(); return false; }
	return true;
}

// ===========================================================================
// Ground truth (manifest.json — flat array of {"file":..,"tonic_idx":..,"mode":..})
// Naive targeted scan, tolerant of key order inside each object.
// ===========================================================================

struct Truth { int tonicIdx = -1; int minor = -1; };  // minor: 0 maj, 1 min

static std::map<std::string, Truth> loadManifest(const std::string& path)
{
	std::map<std::string, Truth> out;
	std::ifstream in(path, std::ios::binary);
	if (!in) return out;
	std::stringstream ss; ss << in.rdbuf();
	const std::string s = ss.str();

	size_t pos = 0;
	while ((pos = s.find('{', pos)) != std::string::npos)
	{
		size_t end = s.find('}', pos);
		if (end == std::string::npos) break;
		const std::string obj = s.substr(pos, end - pos + 1);

		auto strField = [&](const char* key) -> std::string {
			std::string pat = std::string("\"") + key + "\"";
			size_t p = obj.find(pat);
			if (p == std::string::npos) return "";
			p = obj.find(':', p);          if (p == std::string::npos) return "";
			p = obj.find('"', p);          if (p == std::string::npos) return "";
			size_t q = obj.find('"', p + 1); if (q == std::string::npos) return "";
			return obj.substr(p + 1, q - p - 1);
		};
		auto intField = [&](const char* key, int dflt) -> int {
			std::string pat = std::string("\"") + key + "\"";
			size_t p = obj.find(pat);
			if (p == std::string::npos) return dflt;
			p = obj.find(':', p);          if (p == std::string::npos) return dflt;
			return std::atoi(obj.c_str() + p + 1);
		};

		const std::string file = strField("file");
		if (!file.empty())
		{
			Truth t;
			t.tonicIdx = intField("tonic_idx", -1);
			const std::string mode = strField("mode");
			if (!mode.empty()) t.minor = (mode == "min") ? 1 : 0;
			out[file] = t;
		}
		pos = end + 1;
	}
	return out;
}

// ===========================================================================
// MIREX weighting — mirrors diag/key-accuracy/score_keys.py mirex()
// ===========================================================================

static double mirexScore(int predIdx, int predMinor, int trueIdx, int trueMinor,
                         const char** kindOut)
{
	*kindOut = "none";
	if (predIdx < 0 || trueIdx < 0) return 0.0;
	const int d = ((predIdx - trueIdx) % 12 + 12) % 12;
	if (d == 0 && predMinor == trueMinor)              { *kindOut = "exact";    return 1.0; }
	if (predMinor == trueMinor && (d == 7 || d == 5))  { *kindOut = "fifth";    return 0.5; }
	if (predMinor != trueMinor)
	{
		if ((trueMinor == 0 && predMinor == 1 && d == 9) ||
		    (trueMinor == 1 && predMinor == 0 && d == 3)) { *kindOut = "relative"; return 0.3; }
		if (d == 0)                                       { *kindOut = "parallel"; return 0.2; }
	}
	*kindOut = "wrong";
	return 0.0;
}

// ===========================================================================
// Chain result
// ===========================================================================

struct KeyResult
{
	std::string key;        // "" = failed / unknown
	std::string scale;      // "major" / "minor" / ""
	float       strength = 0.0f;
	int         keyIdx   = -1;
	int         minor    = -1;
	std::string error;

	void finish()
	{
		keyIdx = keyStrToIdx(key);
		if      (scale == "major") minor = 0;
		else if (scale == "minor") minor = 1;
	}
};

// ===========================================================================
// (a) PLUGIN CHAIN — the shipped batch worker, called directly.
// ===========================================================================

static KeyResult runPluginChain(const keyoracle::WavData& wav, const Options& o)
{
	KeyResult r;

	EssentiaTD::AudioSnapshot snap;
	snap.data       = wav.mono;
	snap.numSamples = (int)wav.mono.size();
	snap.sampleRate = wav.sampleRate;

	EssentiaTD::BatchTonalParams p;
	p.fftSize         = o.fft;
	p.hopSize         = o.hop;
	p.windowType      = o.window;
	// --zeropad is a FACTOR (like the TD Zeropadding par); BatchTonalParams
	// carries absolute samples — same conversion snapshotAndLaunch does (:740)
	p.zeroPad         = EssentiaTD::zeroPadFromFactor(o.zeroPad, o.fft);
	p.enablePitch     = false;
	p.enablePitchNote = false;
	p.enableHpcp      = false;          // Key computes HPCP internally regardless
	p.hpcpSize        = o.hpcpSize;
	p.enableKey       = true;
	p.keyMode         = o.keyMode;
	p.keyWindowSize   = o.keyWindow;
	p.enableDiss      = false;
	p.enableInharm    = false;
	p.musicalLabels   = true;
	p.peakThreshold   = o.peakThreshold;
	p.peakMaxFreq     = o.peakMaxFreq;
	p.hpcpHarmonics   = o.harmonics;
	p.referenceFreq   = o.tuning;
	p.hpcpNonLinear   = o.nonLinear;
	p.hpcpNormalized  = o.normalizedIdx;
	p.keyProfile      = o.profileIdx;

	std::atomic<bool>  cancel{false};
	std::atomic<float> progress{0.0f};

	EssentiaTD::AsyncBatchResult res =
		EssentiaTD::EssentiaTonalCHOP::computeBatchAsync(snap, p, cancel, progress);

	if (!res.success)          { r.error = res.error.empty() ? "batch failed" : res.error; return r; }
	if (res.numFrames <= 0)    { r.error = "no analysis frames"; return r; }
	if (res.cache.size() < 3)  { r.error = "unexpected channel count"; return r; }

	// Feature flags above make the layout exactly: key, major_minor, key_strength
	// (buildTonalChannelNames, EssentiaTonalCHOP.cpp:76-81).
	// global mode: identical on every frame -> frame 0
	// windowed mode: last frame carries the fullest window
	const int f = (o.keyMode == 0) ? 0 : res.numFrames - 1;

	const int   keyEnc   = (int)std::lround(res.cache[0][f]);
	const float scaleEnc = res.cache[1][f];
	r.strength = res.cache[2][f];
	if (keyEnc >= 0 && keyEnc < 12) r.key = kNotes[keyEnc];
	if      (scaleEnc == 0.0f) r.scale = "major";
	else if (scaleEnc == 1.0f) r.scale = "minor";
	r.finish();
	return r;
}

// ===========================================================================
// (b) ORACLE — standard::KeyExtractor, own defaults.
// The pinned lib excludes algorithms/extractor/, so keyextractor.cpp is
// compiled into this exe and registered here (same Registrar pattern the CI
// registry uses; essentia::init() must have run first).
// ===========================================================================

static void registerKeyExtractor()
{
	// standard first, streaming second is not required — creation happens later
	essentia::standard::AlgorithmFactory::Registrar<
		essentia::standard::KeyExtractor> regStd;
	essentia::streaming::AlgorithmFactory::Registrar<
		essentia::streaming::KeyExtractor,
		essentia::standard::KeyExtractor> regStr;
	(void)regStd; (void)regStr;
}

static KeyResult runOracle(const keyoracle::WavData& wav, const Options& o)
{
	KeyResult r;
	try
	{
		essentia::standard::Algorithm* ke =
			essentia::standard::AlgorithmFactory::create("KeyExtractor",
				"sampleRate",      (Real)wav.sampleRate,
				"frameSize",       o.oracleFrame,
				"hopSize",         o.oracleHop,
				"windowType",      std::string(o.oracleWindow),
				"tuningFrequency", (Real)o.oracleTuning,
				"profileType",     std::string(o.oracleProfile));
		// everything else stays at KeyExtractor defaults:
		// minFrequency 25, maxFrequency 3500, spectralPeaksThreshold 0.0001,
		// maximumSpectralPeaks 60, hpcpSize 12, weightType cosine,
		// pcpThreshold 0.2, averageDetuningCorrection true

		std::vector<Real> audio(wav.mono.begin(), wav.mono.end());
		Real strength = 0.0f;
		ke->input("audio").set(audio);
		ke->output("key").set(r.key);
		ke->output("scale").set(r.scale);
		ke->output("strength").set(strength);
		ke->compute();
		r.strength = (float)strength;
		delete ke;
	}
	catch (const std::exception& e)
	{
		r.error = std::string("oracle: ") + e.what();
	}
	r.finish();
	return r;
}

// ===========================================================================
// (c) EXPERIMENT CHAIN — shipped BatchFrameProcessor + mirrored create()
// lists + D1..D5 toggles. Global key mode only. Baseline (all toggles off)
// must be bit-identical to runPluginChain — enforced by the self-check.
//
// The create() argument lists below intentionally MIRROR
// EssentiaTonalCHOP.cpp computeBatchAsync (:994-1026). If those lists ever
// change without this file, the self-check fails loudly at runtime.
// (Optional source-level dedup: see DESIGN.md "Phase-2 refactor".)
// ===========================================================================

struct ExpEffective    // resolved toggle values, echoed into the JSON config
{
	bool  refKey;
	bool  whiten;
	int   harmonics;
	std::string normalized;
	float hpcpMax, hpcpMin, peakMin;
	bool  gate;
};

static ExpEffective resolveExp(const Options& o, bool baseline)
{
	ExpEffective e;
	static const char* norms[3] = { "unitMax", "unitSum", "none" };
	if (baseline)
	{
		e.refKey = false; e.whiten = false;
		e.harmonics = o.harmonics;
		e.normalized = norms[std::clamp(o.normalizedIdx, 0, 2)];
		e.hpcpMax = 3500.0f; e.hpcpMin = 20.0f; e.peakMin = 20.0f;
		e.gate = false;
	}
	else
	{
		e.refKey     = o.d1RefKey;
		e.whiten     = o.d2Whiten;
		e.harmonics  = (o.d3Harmonics >= 0) ? o.d3Harmonics : o.harmonics;
		e.normalized = !o.d3Normalized.empty()
		             ? o.d3Normalized : norms[std::clamp(o.normalizedIdx, 0, 2)];
		e.hpcpMax    = (o.d4HpcpMax > 0.0f) ? o.d4HpcpMax : 3500.0f;
		e.hpcpMin    = (o.d4HpcpMin > 0.0f) ? o.d4HpcpMin : 20.0f;
		e.peakMin    = (o.d4PeakMin > 0.0f) ? o.d4PeakMin : 20.0f;
		e.gate       = o.d5Gate;
	}
	return e;
}

static KeyResult runExperimentChain(const keyoracle::WavData& wav,
                                    const Options& o, const ExpEffective& e)
{
	using essentia::standard::Algorithm;
	using essentia::standard::AlgorithmFactory;

	KeyResult r;

	// --- framing: the shipped processor, identical config to the plugin call
	EssentiaTD::BatchFrameProcessor frameProc;
	frameProc.configure(o.fft, o.hop, o.window,
	                    EssentiaTD::zeroPadFromFactor(o.zeroPad, o.fft));
	std::atomic<bool> cancel{false};
	if (!frameProc.processAllFrames(wav.mono.data(), (int)wav.mono.size(), &cancel))
	{ r.error = "framing failed"; return r; }

	const int numFrames = frameProc.numFrames();
	if (numFrames == 0) { r.error = "audio too short for FFT size"; return r; }

	const Real sr = (Real)wav.sampleRate;

	Algorithm* peaks = nullptr;
	Algorithm* whiten = nullptr;
	Algorithm* hpcp = nullptr;
	Algorithm* key = nullptr;

	try
	{
		// mirror of computeBatchAsync :994-1000, minFrequency parameterized (D4)
		peaks = AlgorithmFactory::create("SpectralPeaks",
			"sampleRate",         sr,
			"maxPeaks",           60,
			"orderBy",            std::string("magnitude"),
			"magnitudeThreshold", (Real)o.peakThreshold,
			"minFrequency",       (Real)e.peakMin,
			"maxFrequency",       (Real)o.peakMaxFreq);

		if (e.whiten)   // D2 — reference wiring, keyextractor.cpp:63-68 / :109-110
			whiten = AlgorithmFactory::create("SpectralWhitening",
				"maxFrequency", (Real)e.hpcpMax,
				"sampleRate",   sr);

		// mirror of computeBatchAsync :1006-1016, harmonics/normalized/min/max
		// parameterized (D3, D4)
		hpcp = AlgorithmFactory::create("HPCP",
			"size",               o.hpcpSize,
			"sampleRate",         sr,
			"harmonics",          e.harmonics,
			"referenceFrequency", (Real)o.tuning,
			"nonLinear",          o.nonLinear,
			"normalized",         std::string(e.normalized),
			"weightType",         std::string("cosine"),
			"maxFrequency",       (Real)e.hpcpMax,
			"minFrequency",       (Real)e.hpcpMin,
			"bandPreset",         false);

		// mirror of computeBatchAsync :1024-1025 (+ D1 reference key config,
		// keyextractor.cpp:125-132; numHarmonics 4 / slope 0.6 are defaults)
		if (e.refKey)
			key = AlgorithmFactory::create("Key",
				"profileType",    std::string(kProfiles[std::clamp(o.profileIdx, 0, 5)]),
				"usePolyphony",   false,
				"useThreeChords", false,
				"pcpSize",        o.hpcpSize);
		else
			key = AlgorithmFactory::create("Key",
				"profileType",    std::string(kProfiles[std::clamp(o.profileIdx, 0, 5)]));

		// --- per-frame loop, replicating computeBatchAsync's key-relevant path
		//     (:1063-1173) including the ascending re-sort and its exception
		//     handling, so the baseline is operation-for-operation identical
		std::vector<Real> peakFreqs, peakMags;
		std::vector<Real> hpcpBuf((size_t)o.hpcpSize, 0.0f);
		std::vector<std::vector<Real>> allHpcp;
		allHpcp.reserve(numFrames);

		for (int f = 0; f < numFrames; ++f)
		{
			const std::vector<Real>& spectrum = frameProc.getSpectrum(f);

			peakFreqs.clear();
			peakMags.clear();
			try
			{
				peaks->input("spectrum").set(spectrum);
				peaks->output("frequencies").set(peakFreqs);
				peaks->output("magnitudes").set(peakMags);
				peaks->compute();

				// ascending re-sort — mirrors :1121-1135 (plugin does this for
				// Dissonance/Inharmonicity; HPCP is order-independent, kept for
				// bit-exact baseline parity)
				if (peakFreqs.size() > 1)
				{
					std::vector<size_t> idx(peakFreqs.size());
					std::iota(idx.begin(), idx.end(), 0);
					std::sort(idx.begin(), idx.end(),
						[&](size_t a, size_t b) { return peakFreqs[a] < peakFreqs[b]; });
					std::vector<Real> sortedF(peakFreqs.size()), sortedM(peakMags.size());
					for (size_t i = 0; i < idx.size(); ++i)
					{
						sortedF[i] = peakFreqs[idx[i]];
						sortedM[i] = peakMags[idx[i]];
					}
					peakFreqs = std::move(sortedF);
					peakMags  = std::move(sortedM);
				}
			}
			catch (...) { peakFreqs.clear(); peakMags.clear(); }

			// D2: whiten peak magnitudes against the spectrum
			if (e.whiten && !peakFreqs.empty())
			{
				try
				{
					std::vector<Real> whiteMags;
					whiten->input("spectrum").set(spectrum);
					whiten->input("frequencies").set(peakFreqs);
					whiten->input("magnitudes").set(peakMags);
					whiten->output("magnitudes").set(whiteMags);
					whiten->compute();
					peakMags = std::move(whiteMags);
				}
				catch (...) { /* keep unwhitened mags */ }
			}

			hpcpBuf.assign((size_t)o.hpcpSize, 0.0f);
			if (!peakFreqs.empty())
			{
				try
				{
					hpcp->input("frequencies").set(peakFreqs);
					hpcp->input("magnitudes").set(peakMags);
					hpcp->output("hpcp").set(hpcpBuf);
					hpcp->compute();
				}
				catch (...) { hpcpBuf.assign((size_t)o.hpcpSize, 0.0f); }
			}

			allHpcp.push_back(hpcpBuf);
		}

		// --- global average, replicating :1245-1253 (sum order + float invN)
		const size_t hLen = allHpcp.front().size();
		std::vector<Real> keyPcp(hLen, 0.0f);
		for (const auto& frame : allHpcp)
			for (size_t i = 0; i < hLen && i < frame.size(); ++i)
				keyPcp[i] += frame[i];
		const float invN = 1.0f / (float)allHpcp.size();
		for (auto& v : keyPcp) v *= invN;

		// D5: streaming::Key preprocessing (key.cpp:666-669 — normalize to
		// peak, then zero bins < 0.2). shiftPcp (averageDetuningCorrection) is
		// a structural no-op at hpcpSize 12 (tuningResolution = 1) and is NOT
		// replicated; the harness refuses d5 with hpcpSize > 12.
		if (e.gate)
		{
			if (o.hpcpSize > 12)
			{ r.error = "--d5-gate implemented for hpcpsize=12 only (shiftPcp not replicated)"; return r; }
			Real mx = 0.0f;
			for (Real v : keyPcp) mx = std::max(mx, v);
			if (mx > 0.0f)
				for (auto& v : keyPcp) v /= mx;
			for (auto& v : keyPcp)
				if (v < 0.2f) v = 0.0f;
		}

		Real strength = 0.0f, firstToSecond = 0.0f;
		key->input("pcp").set(keyPcp);
		key->output("key").set(r.key);
		key->output("scale").set(r.scale);
		key->output("strength").set(strength);
		key->output("firstToSecondRelativeStrength").set(firstToSecond);
		key->compute();
		r.strength = (float)strength;
	}
	catch (const std::exception& ex)
	{
		r.error = std::string("experiment: ") + ex.what();
	}

	delete peaks; delete whiten; delete hpcp; delete key;
	r.finish();
	return r;
}

// ===========================================================================
// JSON emission — fixed field order, fixed float format, no timestamps.
// ===========================================================================

static std::string jf(double v)                 // fixed float format
{
	char b[64];
	std::snprintf(b, sizeof(b), "%.6f", v);
	return b;
}

static std::string js(const std::string& s)     // minimal JSON string escape
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
		else out += c;
	}
	out += '"';
	return out;
}

static std::string chainJson(const KeyResult& r)
{
	std::string s = "{";
	s += "\"key\":" + js(r.key) + ",\"scale\":" + js(r.scale);
	s += ",\"strength\":" + jf(r.strength);
	s += ",\"key_idx\":" + std::to_string(r.keyIdx);
	s += ",\"minor\":" + std::to_string(r.minor);
	if (!r.error.empty()) s += ",\"error\":" + js(r.error);
	s += "}";
	return s;
}

static std::string configJson(const Options& o, const ExpEffective* e)
{
	static const char* norms[3] = { "unitMax", "unitSum", "none" };
	std::string s = "{";
	s += "\"profile\":" + js(kProfiles[std::clamp(o.profileIdx, 0, 5)]);
	s += ",\"fft\":" + std::to_string(o.fft) + ",\"hop\":" + std::to_string(o.hop);
	s += ",\"window\":" + js(o.window) + ",\"zeropad\":" + std::to_string(o.zeroPad);
	s += ",\"keymode\":" + js(o.keyMode == 0 ? "global" : "windowed");
	s += ",\"keywindow\":" + std::to_string(o.keyWindow);
	s += ",\"hpcpsize\":" + std::to_string(o.hpcpSize);
	s += ",\"tuning\":" + jf(o.tuning);
	s += ",\"peak_threshold\":" + jf(o.peakThreshold);
	s += ",\"peak_maxfreq\":" + jf(o.peakMaxFreq);
	s += ",\"harmonics\":" + std::to_string(o.harmonics);
	s += ",\"normalized\":" + js(norms[std::clamp(o.normalizedIdx, 0, 2)]);
	s += ",\"nonlinear\":" + std::string(o.nonLinear ? "true" : "false");
	s += ",\"oracle\":{\"profile\":" + js(o.oracleProfile)
	   + ",\"frame\":" + std::to_string(o.oracleFrame)
	   + ",\"hop\":" + std::to_string(o.oracleHop)
	   + ",\"window\":" + js(o.oracleWindow)
	   + ",\"tuning\":" + jf(o.oracleTuning) + "}";
	if (e)
	{
		s += ",\"exp\":{\"d1_refkey\":" + std::string(e->refKey ? "true" : "false");
		s += ",\"d2_whiten\":" + std::string(e->whiten ? "true" : "false");
		s += ",\"d3_harmonics\":" + std::to_string(e->harmonics);
		s += ",\"d3_normalized\":" + js(e->normalized);
		s += ",\"d4_hpcp_maxfreq\":" + jf(e->hpcpMax);
		s += ",\"d4_hpcp_minfreq\":" + jf(e->hpcpMin);
		s += ",\"d4_peak_minfreq\":" + jf(e->peakMin);
		s += ",\"d5_gate\":" + std::string(e->gate ? "true" : "false") + "}";
	}
	s += ",\"essentia\":" + js(essentia::version);
	s += "}";
	return s;
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char** argv)
{
	std::setlocale(LC_ALL, "C");   // printf decimal point must not vary

	Options o;
	if (!parseArgs(argc, argv, o)) return 1;

	std::string initErr;
	if (!EssentiaTD::ensureEssentiaInit(initErr))
	{ std::fprintf(stderr, "%s\n", initErr.c_str()); return 1; }
	registerKeyExtractor();

	std::map<std::string, Truth> manifest;
	if (!o.manifestPath.empty())
	{
		manifest = loadManifest(o.manifestPath);
		if (manifest.empty())
			std::fprintf(stderr, "warning: no entries parsed from %s\n",
			             o.manifestPath.c_str());
	}

	FILE* out = stdout;
	if (!o.outPath.empty())
	{
#ifdef _WIN32
		fopen_s(&out, o.outPath.c_str(), "wb");
#else
		out = std::fopen(o.outPath.c_str(), "wb");
#endif
		if (!out) { std::fprintf(stderr, "cannot open %s\n", o.outPath.c_str()); return 1; }
	}

	int rc = 0;

	for (const std::string& path : o.files)
	{
		keyoracle::WavData wav = keyoracle::readWavMono(path);
		if (!wav.error.empty())
		{
			std::fprintf(stderr, "%s: %s\n", path.c_str(), wav.error.c_str());
			rc = 1;
			continue;
		}

		const KeyResult plugin = runPluginChain(wav, o);
		const KeyResult oracle = runOracle(wav, o);

		// --- drift guard: baseline experiment must equal shipped plugin path
		std::string selfCheck = "skipped";
		if (o.selfCheck)
		{
			if (o.keyMode != 0)
				selfCheck = "skipped_windowed";   // experiment chain is global-only
			else
			{
				const ExpEffective base = resolveExp(o, /*baseline=*/true);
				const KeyResult chk = runExperimentChain(wav, o, base);
				// Compare pitch CLASS, not spelling: the plugin column decodes
				// through Shared/Utils.h's sharp table while the experiment
				// chain reports essentia's raw string, which uses flats. "A#"
				// vs "Bb" is the same key and must not read as drift.
				const bool same = chk.keyIdx == plugin.keyIdx
				               && chk.keyIdx >= 0
				               && chk.scale == plugin.scale
				               && chk.strength == plugin.strength;   // bit-exact
				selfCheck = same ? "exact" : "DRIFT";
				if (!same)
				{
					std::fprintf(stderr,
						"DRIFT on %s: plugin %s %s %.9g vs baseline-experiment %s %s %.9g\n"
						"The mirrored create() lists in key_oracle_main.cpp no longer match\n"
						"EssentiaTonalCHOP.cpp computeBatchAsync. Fix before trusting any sweep.\n",
						path.c_str(),
						plugin.key.c_str(), plugin.scale.c_str(), (double)plugin.strength,
						chk.key.c_str(), chk.scale.c_str(), (double)chk.strength);
					rc = 2;
				}
			}
		}

		// --- experiment chain with the requested toggles
		KeyResult expr;
		ExpEffective eff{};
		bool haveExp = false;
		if (o.exp && o.keyMode == 0)
		{
			eff = resolveExp(o, /*baseline=*/false);
			expr = runExperimentChain(wav, o, eff);
			haveExp = true;
		}

		// --- agreement + scores
		const char* kindPO = "none";
		const double mirexPO = mirexScore(plugin.keyIdx, plugin.minor,
		                                  oracle.keyIdx, oracle.minor, &kindPO);
		const bool exactPO = plugin.keyIdx >= 0
		                  && plugin.keyIdx == oracle.keyIdx
		                  && plugin.minor == oracle.minor;

		// truth lookup by basename
		std::string base = path;
		{
			size_t sl = base.find_last_of("/\\");
			if (sl != std::string::npos) base = base.substr(sl + 1);
		}
		const Truth* truth = nullptr;
		auto it = manifest.find(base);
		if (it == manifest.end()) it = manifest.find(path);
		if (it != manifest.end()) truth = &it->second;

		// --- emit row (fixed order, single line)
		std::string row = "{";
		row += "\"file\":" + js(path);
		row += ",\"sr\":" + jf(wav.sampleRate);
		row += ",\"num_samples\":" + std::to_string((long long)wav.mono.size());
		row += ",\"src\":{\"channels\":" + std::to_string(wav.srcChannels)
		     + ",\"bits\":" + std::to_string(wav.srcBits)
		     + ",\"float\":" + (wav.srcFloat ? std::string("true") : std::string("false")) + "}";
		row += ",\"plugin\":" + chainJson(plugin);
		row += ",\"oracle\":" + chainJson(oracle);
		row += ",\"experiment\":" + (haveExp ? chainJson(expr) : std::string("null"));
		row += ",\"selfcheck\":" + js(selfCheck);
		row += ",\"agreement\":{\"exact\":" + std::string(exactPO ? "true" : "false")
		     + ",\"mirex\":" + jf(mirexPO)
		     + ",\"kind\":" + js(kindPO) + "}";

		if (truth && truth->tonicIdx >= 0 && truth->minor >= 0)
		{
			const char* k1; const char* k2; const char* k3;
			const double mP = mirexScore(plugin.keyIdx, plugin.minor,
			                             truth->tonicIdx, truth->minor, &k1);
			const double mO = mirexScore(oracle.keyIdx, oracle.minor,
			                             truth->tonicIdx, truth->minor, &k2);
			row += ",\"truth\":{\"key\":" + js(kNotes[truth->tonicIdx])
			     + ",\"scale\":" + js(truth->minor ? "minor" : "major") + "}";
			row += ",\"scores\":{\"plugin_mirex\":" + jf(mP)
			     + ",\"plugin_kind\":" + js(k1)
			     + ",\"oracle_mirex\":" + jf(mO)
			     + ",\"oracle_kind\":" + js(k2);
			if (haveExp)
			{
				const double mE = mirexScore(expr.keyIdx, expr.minor,
				                             truth->tonicIdx, truth->minor, &k3);
				row += ",\"experiment_mirex\":" + jf(mE)
				     + ",\"experiment_kind\":" + js(k3);
			}
			row += "}";
		}
		else
		{
			row += ",\"truth\":null,\"scores\":null";
		}

		row += ",\"config\":" + configJson(o, haveExp ? &eff : nullptr);
		row += "}";
		std::fprintf(out, "%s\n", row.c_str());
	}

	if (out != stdout) std::fclose(out);
	essentia::shutdown();
	return rc;
}
