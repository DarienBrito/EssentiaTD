#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Minimal RIFF/WAVE reader for the key_oracle harness.
//
// Why not Essentia MonoLoader/AudioLoader: the pinned static lib is built by
// ci/essentia-CMakeLists.txt, whose algorithm globs cover only
// standard/spectral/tonal/rhythm/temporal/stats/complex/filters —
// src/algorithms/io/ (audioloader, monoloader; libav-dependent) is never
// compiled, and ci/essentia_algorithms_reg.cpp registers neither. Verified
// 2026-08-11 by grep of both files. So audio loading must be lib-independent.
//
// Supports: PCM 16/24/32-bit int, IEEE float32, WAVE_FORMAT_EXTENSIBLE
// wrapping either. Multi-channel is downmixed to mono by arithmetic mean of
// all channels ((L+R)/2 for stereo — matches MonoLoader "mix" downmix and the
// input conditioning of the recorded TD baselines; see MEMORY note on
// chain-specific key_strength baselines).
//
// Deterministic: pure integer/float arithmetic in file order, no threads.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace keyoracle
{

struct WavData
{
	std::vector<float> mono;      // downmixed samples, [-1, 1]
	double             sampleRate = 0.0;
	int                srcChannels = 0;
	int                srcBits     = 0;   // 16/24/32
	bool               srcFloat    = false;
	std::string        error;             // non-empty on failure
};

namespace detail
{
inline uint32_t rdU32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t rdU16(const uint8_t* p) {
	return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
} // namespace detail

inline WavData readWavMono(const std::string& path)
{
	using namespace detail;
	WavData out;

	FILE* f = nullptr;
#ifdef _WIN32
	fopen_s(&f, path.c_str(), "rb");
#else
	f = std::fopen(path.c_str(), "rb");
#endif
	if (!f) { out.error = "cannot open file"; return out; }

	std::vector<uint8_t> buf;
	{
		std::fseek(f, 0, SEEK_END);
		long sz = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (sz < 44) { std::fclose(f); out.error = "file too small"; return out; }
		buf.resize((size_t)sz);
		size_t got = std::fread(buf.data(), 1, (size_t)sz, f);
		std::fclose(f);
		if (got != (size_t)sz) { out.error = "short read"; return out; }
	}

	if (std::memcmp(buf.data(), "RIFF", 4) != 0 ||
	    std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
	{ out.error = "not a RIFF/WAVE file"; return out; }

	// Walk chunks
	uint16_t fmtTag = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	const uint8_t* dataPtr = nullptr;
	uint32_t dataLen = 0;
	bool haveFmt = false;

	size_t pos = 12;
	while (pos + 8 <= buf.size())
	{
		const uint8_t* ck = buf.data() + pos;
		uint32_t ckLen = rdU32(ck + 4);
		const uint8_t* body = ck + 8;
		if (pos + 8 + ckLen > buf.size())
			ckLen = (uint32_t)(buf.size() - pos - 8);   // tolerate truncated tail

		if (std::memcmp(ck, "fmt ", 4) == 0 && ckLen >= 16)
		{
			fmtTag   = rdU16(body + 0);
			channels = rdU16(body + 2);
			rate     = rdU32(body + 4);
			bits     = rdU16(body + 14);
			if (fmtTag == 0xFFFE && ckLen >= 40)          // WAVE_FORMAT_EXTENSIBLE
				fmtTag = rdU16(body + 24);                 // first 2 bytes of SubFormat GUID
			haveFmt = true;
		}
		else if (std::memcmp(ck, "data", 4) == 0)
		{
			dataPtr = body;
			dataLen = ckLen;
		}
		pos += 8 + ckLen + (ckLen & 1);                   // chunks are word-aligned
	}

	if (!haveFmt)  { out.error = "no fmt chunk";  return out; }
	if (!dataPtr)  { out.error = "no data chunk"; return out; }
	if (channels < 1 || rate == 0) { out.error = "bad fmt values"; return out; }

	const bool isFloat = (fmtTag == 3);
	const bool isPcm   = (fmtTag == 1);
	if (!isFloat && !isPcm)
	{ out.error = "unsupported format tag " + std::to_string(fmtTag); return out; }
	if (isFloat && bits != 32)
	{ out.error = "unsupported float bit depth " + std::to_string(bits); return out; }
	if (isPcm && bits != 16 && bits != 24 && bits != 32)
	{ out.error = "unsupported PCM bit depth " + std::to_string(bits); return out; }

	const uint32_t bytesPerSample = bits / 8;
	const uint32_t frameBytes = bytesPerSample * channels;
	const uint32_t numFrames = frameBytes ? dataLen / frameBytes : 0;
	if (numFrames == 0) { out.error = "empty data chunk"; return out; }

	out.mono.resize(numFrames);
	const double invCh = 1.0 / (double)channels;

	for (uint32_t i = 0; i < numFrames; ++i)
	{
		const uint8_t* fr = dataPtr + (size_t)i * frameBytes;
		double acc = 0.0;
		for (uint16_t c = 0; c < channels; ++c)
		{
			const uint8_t* sp = fr + (size_t)c * bytesPerSample;
			double v = 0.0;
			if (isFloat)
			{
				float fv;
				std::memcpy(&fv, sp, 4);
				v = (double)fv;
			}
			else if (bits == 16)
			{
				int16_t s = (int16_t)rdU16(sp);
				v = (double)s / 32768.0;
			}
			else if (bits == 24)
			{
				int32_t s = (int32_t)((uint32_t)sp[0] << 8 | (uint32_t)sp[1] << 16
				                    | (uint32_t)sp[2] << 24) >> 8;   // sign-extend
				v = (double)s / 8388608.0;
			}
			else // 32-bit int PCM
			{
				int32_t s = (int32_t)rdU32(sp);
				v = (double)s / 2147483648.0;
			}
			acc += v;
		}
		out.mono[i] = (float)(acc * invCh);
	}

	out.sampleRate  = (double)rate;
	out.srcChannels = channels;
	out.srcBits     = bits;
	out.srcFloat    = isFloat;
	return out;
}

} // namespace keyoracle
