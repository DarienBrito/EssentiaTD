#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Header-only windowing + FFT loop for batch analysis CHOPs.
// Processes an entire audio buffer and stores all magnitude spectra.

#include <essentia/algorithmfactory.h>
#include <atomic>
#include <cassert>
#include <complex>
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

		myFFT.reset(essentia::standard::AlgorithmFactory::create("FFT", "size", paddedSize));
		myCartToPolar.reset(essentia::standard::AlgorithmFactory::create("CartesianToPolar"));

		myFftBuf.resize(mySpecBins);
		myPhaseBuf.resize(mySpecBins, 0.0f);
	}

	/// Process all frames from the audio buffer and store spectra.
	/// Checks `cancelFlag` (if given) every 64 frames so AsyncBatchRunner's
	/// cancelAndWait() — fired on re-trigger or CHOP deletion — can't be
	/// blocked for the remainder of a long FFT pass.
	/// Returns false if cancelled (stored frames are then invalid).
	bool processAllFrames(const float* audio, int audioLength,
	                      const std::atomic<bool>* cancelFlag = nullptr)
	{
		myNumFrames = (audioLength >= myFftSize)
			? (audioLength - myFftSize) / myHopSize + 1
			: 0;

		mySpectra.resize(myNumFrames);
		myPhases.resize(myNumFrames);

		for (int f = 0; f < myNumFrames; ++f)
		{
			if (cancelFlag && (f & 63) == 0 &&
			    cancelFlag->load(std::memory_order_relaxed))
			{
				myNumFrames = 0;
				return false;
			}

			const int offset = f * myHopSize;

			// Copy audio frame
			for (int i = 0; i < myFftSize; ++i)
				myAudioFrame[i] = static_cast<essentia::Real>(audio[offset + i]);

			// Windowing
			myWindowing->input("frame").set(myAudioFrame);
			myWindowing->output("frame").set(myWindowedFrame);
			myWindowing->compute();

			// FFT
			myFFT->input("frame").set(myWindowedFrame);
			myFFT->output("fft").set(myFftBuf);
			myFFT->compute();

			// CartesianToPolar -> magnitude + phase
			myCartToPolar->input("complex").set(myFftBuf);
			myCartToPolar->output("magnitude").set(mySpectrumBuf);
			myCartToPolar->output("phase").set(myPhaseBuf);
			myCartToPolar->compute();

			// Store copies
			mySpectra[f] = mySpectrumBuf;
			myPhases[f]  = myPhaseBuf;
		}
		return true;
	}

	/// Access spectrum at a given frame index.
	const std::vector<essentia::Real>& getSpectrum(int frameIdx) const
	{
		assert(frameIdx >= 0 && frameIdx < myNumFrames);
		return mySpectra[frameIdx];
	}

	/// Access phase spectrum at a given frame index.
	const std::vector<essentia::Real>& getPhase(int frameIdx) const
	{
		assert(frameIdx >= 0 && frameIdx < myNumFrames);
		return myPhases[frameIdx];
	}

	int numFrames() const { return myNumFrames; }
	int specBins()  const { return mySpecBins; }
	int fftSize()   const { return myFftSize; }
	int hopSize()   const { return myHopSize; }

	void release()
	{
		myWindowing.reset();
		myFFT.reset();
		myCartToPolar.reset();
		mySpectra.clear();
		myPhases.clear();
		myNumFrames = 0;
	}

private:
	std::unique_ptr<essentia::standard::Algorithm> myWindowing;
	std::unique_ptr<essentia::standard::Algorithm> myFFT;
	std::unique_ptr<essentia::standard::Algorithm> myCartToPolar;

	std::vector<essentia::Real>                    myAudioFrame;
	std::vector<essentia::Real>                    myWindowedFrame;
	std::vector<std::complex<essentia::Real>>      myFftBuf;
	std::vector<essentia::Real>                    mySpectrumBuf;
	std::vector<essentia::Real>                    myPhaseBuf;

	std::vector<std::vector<essentia::Real>>       mySpectra;
	std::vector<std::vector<essentia::Real>>       myPhases;

	int myFftSize     = 0;
	int myHopSize     = 0;
	int myZeroPadding = 0;
	int mySpecBins    = 0;
	int myNumFrames   = 0;
};

} // namespace EssentiaTD
