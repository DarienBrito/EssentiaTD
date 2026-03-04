#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <vector>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace EssentiaTD
{

/// Fixed-size ring buffer for accumulating audio samples.
/// Single-producer / single-consumer safe (TD cooks on one thread per CHOP).
class RingBuffer
{
public:
	RingBuffer() = default;

	explicit RingBuffer(size_t capacity)
		: myBuffer(capacity, 0.0f)
	{
	}

	void resize(size_t capacity)
	{
		myBuffer.assign(capacity, 0.0f);
		myWritePos = 0;
		myReadPos = 0;
		myCount = 0;
	}

	size_t capacity() const { return myBuffer.size(); }
	size_t available() const { return myCount; }
	bool empty() const { return myCount == 0; }

	/// Push samples into the ring buffer.
	void write(const float* data, size_t numSamples)
	{
		if (myBuffer.empty()) return;
		for (size_t i = 0; i < numSamples; ++i)
		{
			myBuffer[myWritePos] = data[i];
			myWritePos = (myWritePos + 1) % myBuffer.size();
			if (myCount < myBuffer.size())
				++myCount;
			else
				myReadPos = (myReadPos + 1) % myBuffer.size(); // overwrite oldest
		}
	}

	/// Read the most recent `numSamples` into `dest` (ordered oldest-to-newest).
	/// Returns number of samples actually read (may be less than requested if buffer not full).
	size_t readLatest(float* dest, size_t numSamples) const
	{
		if (myBuffer.empty()) return 0;
		size_t toRead = std::min(numSamples, myCount);
		// Start position: writePos - toRead (wrapped)
		size_t start = (myWritePos + myBuffer.size() - toRead) % myBuffer.size();
		for (size_t i = 0; i < toRead; ++i)
		{
			dest[i] = myBuffer[(start + i) % myBuffer.size()];
		}
		return toRead;
	}

	/// Read the most recent `numSamples` into a std::vector<float>.
	void readLatest(std::vector<float>& dest, size_t numSamples) const
	{
		dest.resize(numSamples);
		size_t got = readLatest(dest.data(), numSamples);
		if (got < numSamples)
		{
			// zero-pad the beginning if not enough samples
			std::memmove(dest.data() + (numSamples - got), dest.data(), got * sizeof(float));
			std::memset(dest.data(), 0, (numSamples - got) * sizeof(float));
		}
	}

	void clear()
	{
		myWritePos = 0;
		myReadPos = 0;
		myCount = 0;
	}

private:
	std::vector<float> myBuffer;
	size_t myWritePos = 0;
	size_t myReadPos = 0;
	size_t myCount = 0;
};

} // namespace EssentiaTD
