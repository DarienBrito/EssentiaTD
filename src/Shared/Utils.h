#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

// Forward-declare TD types
namespace TD { class OP_CHOPInput; }

namespace EssentiaTD
{

/// Extract channels matching a given prefix from a TD CHOP input.
/// E.g. prefix "spec_mag" matches "spec_mag0", "spec_mag1", ...
/// Results are placed in `out` ordered by channel index.
inline bool extractChannelsByPrefix(
	const TD::OP_CHOPInput* input,
	const char* prefix,
	std::vector<float>& out)
{
	if (!input) return false;

	out.clear();
	size_t prefixLen = std::strlen(prefix);

	for (int32_t i = 0; i < input->numChannels; ++i)
	{
		const char* name = input->getChannelName(i);
		if (std::strncmp(name, prefix, prefixLen) == 0)
		{
			// Take the first sample of this channel (analysis frame = 1 sample)
			out.push_back(input->getChannelData(i)[0]);
		}
	}
	return !out.empty();
}

/// Extract all samples of a single named channel from a TD CHOP input.
/// Used when a channel carries multi-sample data (e.g. spectrum bins, mel bands).
inline bool extractChannelSamples(
	const TD::OP_CHOPInput* input,
	const char* channelName,
	std::vector<float>& out)
{
	if (!input) return false;
	for (int32_t i = 0; i < input->numChannels; ++i)
	{
		if (std::strcmp(input->getChannelName(i), channelName) == 0)
		{
			out.resize(input->numSamples);
			const float* data = input->getChannelData(i);
			for (int s = 0; s < input->numSamples; ++s)
				out[s] = data[s];
			return true;
		}
	}
	out.clear();
	return false;
}

/// Find a single named channel and return its first sample value.
/// Returns `defaultVal` if not found.
inline float getChannelValue(
	const TD::OP_CHOPInput* input,
	const char* channelName,
	float defaultVal = 0.0f)
{
	if (!input) return defaultVal;

	for (int32_t i = 0; i < input->numChannels; ++i)
	{
		if (std::strcmp(input->getChannelName(i), channelName) == 0)
			return input->getChannelData(i)[0];
	}
	return defaultVal;
}

/// Encode a key string ("C", "C#", ..., "B") as a float 0-11.
/// Returns -1 for unknown keys.
inline float encodeKey(const std::string& key)
{
	static const char* keys[] = {
		"C", "C#", "D", "D#", "E", "F",
		"F#", "G", "G#", "A", "A#", "B"
	};
	// Also handle "Db", "Eb", etc. via sharps
	static const char* flats[] = {
		"", "Db", "", "Eb", "", "",
		"Gb", "", "Ab", "", "Bb", ""
	};
	for (int i = 0; i < 12; ++i)
	{
		if (key == keys[i]) return (float)i;
		if (flats[i][0] && key == flats[i]) return (float)i;
	}
	return -1.0f;
}

/// Frequency ratio of one equal-tempered semitone (2^(1/12)).
constexpr double kSemitoneRatio = 1.0594630943592953;

/// Reference pitch the resolution check is judged at: middle C (C4).
/// Chosen because it sits in the register that carries a piece's harmony —
/// resolving semitones only ABOVE middle C is not enough for HPCP/key.
constexpr double kResolutionRefHz = 261.63;

/// Lowest frequency at which a spectrum of the given bin spacing can still
/// separate adjacent semitones. A semitone at f is f*(2^(1/12)-1) Hz wide, so
/// anything below binSpacing/(2^(1/12)-1) collapses neighbouring pitches into
/// one bin.
inline double lowestResolvableSemitoneHz(double binSpacingHz)
{
	return binSpacingHz / (kSemitoneRatio - 1.0);
}

/// Warn when a spectrum is too coarse for HPCP/key to be trustworthy.
///
/// WHY THIS EXISTS: below ~4096 FFT (at 44.1/48 kHz) the spectrum cannot
/// separate adjacent semitones in the register that carries harmony, so HPCP
/// lands energy in the wrong pitch classes and the key can come out a
/// SEMITONE WRONG. Measured (docs/tonal-fft-resolution.md): batch key
/// detection went 0/3 -> 3/3 correct tracks sweeping 512 -> 4096.
///
/// The failure is silent and looks trustworthy: on one test track a 1024
/// spectrum reported the WRONG key at key_strength 0.608 while the CORRECT
/// answer at 4096 reported only 0.531 — confidence cannot flag it. Realtime
/// degrades rather than breaks (81.7% correct frames at 1024 vs 91.3% at
/// 4096 on the same material). Hence a warning rather than relying on the
/// user to notice.
///
/// CAVEAT: this measures bin SPACING. When the caller derives spacing from a
/// padded bin count, zero-padding narrows spacing without improving true
/// resolution and can defeat the check — callers that know the true analysis
/// window (batch, internal FFT) must compute spacing from fftSize, never
/// from padded bins.
///
/// Returns an empty string when the resolution is adequate.
inline std::string spectrumResolutionWarning(double binSpacingHz)
{
	if (binSpacingHz <= 0.0)
		return std::string();

	const double lowest = lowestResolvableSemitoneHz(binSpacingHz);
	if (lowest <= kResolutionRefHz)
		return std::string();

	char buf[256];
	std::snprintf(buf, sizeof(buf),
		"Input spectrum too coarse for key/HPCP: %.1f Hz per bin resolves "
		"semitones only above %.0f Hz. Key may be wrong by a semitone. "
		"Increase FFT size upstream (4096 recommended).",
		binSpacingHz, lowest);
	return std::string(buf);
}

} // namespace EssentiaTD
