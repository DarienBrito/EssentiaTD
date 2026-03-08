#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Header-only windowing + FFT loop for batch analysis CHOPs.
// Processes an entire audio buffer and stores all magnitude spectra.

#include <essentia/algorithmfactory.h>
#include <cassert>
#include <memory>
#include <vector>
#include <string>

namespace EssentiaTD
{

class BatchFrameProcessor
{
public:
	~BatchFrameProcessor() { release(); }

	/// Configure windowing and spectrum algorithms.
	/// Call before processAllFrames().
	void configure(int fftSize, int hopSize,
	               const std::string& windowType, int zeroPadding)
	{
		release();

		myFftSize     = fftSize;
		myHopSize     = hopSize;
		myZeroPadding = zeroPadding;

		const int paddedSize = fftSize + zeroPadding;
		mySpecBins = paddedSize / 2 + 1;

		myAudioFrame.resize(fftSize, 0.0f);
		myWindowedFrame.resize(paddedSize, 0.0f);
		mySpectrumBuf.resize(mySpecBins, 0.0f);

		myWindowing.reset(essentia::standard::AlgorithmFactory::create("Windowing",
			"type",        windowType,
			"size",        fftSize,
			"zeroPadding", zeroPadding,
			"normalized",  true));

		mySpectrumAlgo.reset(essentia::standard::AlgorithmFactory::create("Spectrum",
			"size", paddedSize));
	}

	/// Process all frames from the audio buffer and store spectra.
	void processAllFrames(const float* audio, int audioLength)
	{
		myNumFrames = (audioLength >= myFftSize)
			? (audioLength - myFftSize) / myHopSize + 1
			: 0;

		mySpectra.resize(myNumFrames);

		for (int f = 0; f < myNumFrames; ++f)
		{
			const int offset = f * myHopSize;

			// Copy audio frame
			for (int i = 0; i < myFftSize; ++i)
				myAudioFrame[i] = static_cast<essentia::Real>(audio[offset + i]);

			// Windowing
			myWindowing->input("frame").set(myAudioFrame);
			myWindowing->output("frame").set(myWindowedFrame);
			myWindowing->compute();

			// Spectrum (magnitude)
			mySpectrumAlgo->input("frame").set(myWindowedFrame);
			mySpectrumAlgo->output("spectrum").set(mySpectrumBuf);
			mySpectrumAlgo->compute();

			// Store copy
			mySpectra[f] = mySpectrumBuf;
		}
	}

	/// Access spectrum at a given frame index.
	const std::vector<essentia::Real>& getSpectrum(int frameIdx) const
	{
		assert(frameIdx >= 0 && frameIdx < myNumFrames);
		return mySpectra[frameIdx];
	}

	int numFrames() const { return myNumFrames; }
	int specBins()  const { return mySpecBins; }
	int fftSize()   const { return myFftSize; }
	int hopSize()   const { return myHopSize; }

	void release()
	{
		myWindowing.reset();
		mySpectrumAlgo.reset();
		mySpectra.clear();
		myNumFrames = 0;
	}

private:
	std::unique_ptr<essentia::standard::Algorithm> myWindowing;
	std::unique_ptr<essentia::standard::Algorithm> mySpectrumAlgo;

	std::vector<essentia::Real> myAudioFrame;
	std::vector<essentia::Real> myWindowedFrame;
	std::vector<essentia::Real> mySpectrumBuf;

	std::vector<std::vector<essentia::Real>> mySpectra;

	int myFftSize     = 0;
	int myHopSize     = 0;
	int myZeroPadding = 0;
	int mySpecBins    = 0;
	int myNumFrames   = 0;
};

} // namespace EssentiaTD
