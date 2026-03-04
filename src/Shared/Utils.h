#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <vector>
#include <string>
#include <cstring>

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

} // namespace EssentiaTD
