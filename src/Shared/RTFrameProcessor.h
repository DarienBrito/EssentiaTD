#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RTFrameProcessor — realtime frame machinery for every op that runs its own
// FFT per cook: ring-buffer accumulation across input timeslices, then
// Windowing -> FFT -> CartesianToPolar over the latest full window.
//
// Extracted from EssentiaSpectrumCHOP for the v2.0 per-op-analysis redesign
// (each analyzer owns its FFT in realtime, mirroring what BatchFrameProcessor
// already does offline).
//
// Deliberately TD-type-free: accumulate() takes raw samples plus the input's
// cook counter, so this header runs headless (see tests/ golden-vector test).
// The consuming CHOP passes audioIn->getChannelData(0) / numSamples /
// totalCooks.

#include "RingBuffer.h"

#include <essentia/algorithmfactory.h>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace EssentiaTD
{

class RTFrameProcessor
{
public:
	~RTFrameProcessor() { release(); }

	/// (Re)build algorithms and buffers. Throws on factory error; the caller
	/// catches, reports, and leaves its tracking state unchanged so the
	/// configure retries next cook (EssentiaSpectrumCHOP pattern — valid
	/// menu combinations do not throw, so this cannot retry-storm).
	void configure(int fftSize, const std::string& windowType, int zeroPadding)
	{
		release();

		myFftSize = fftSize;
		const int paddedSize = fftSize + zeroPadding;
		mySpecBins = paddedSize / 2 + 1;

		myAudioFrame.resize(static_cast<size_t>(fftSize), 0.0f);
		myWindowedFrame.resize(static_cast<size_t>(paddedSize), 0.0f);
		myFftBuf.resize(static_cast<size_t>(mySpecBins));
		mySpectrumMag.assign(static_cast<size_t>(mySpecBins), 0.0f);
		myPhase.assign(static_cast<size_t>(mySpecBins), 0.0f);

		// Ring holds exactly one analysis window — with latest-window-per-cook
		// semantics, samples older than one window are never read, so a larger
		// ring buys nothing. Resize clears accumulated audio.
		myAudioRing.resize(static_cast<size_t>(fftSize));
		myLastInputCook = -1;
		myLastTimeslice = 0;

		myWindowing = essentia::standard::AlgorithmFactory::create("Windowing",
			"type",        windowType,
			"size",        fftSize,
			"zeroPadding", zeroPadding,
			"normalized",  true);

		myFFT = essentia::standard::AlgorithmFactory::create("FFT",
			"size", paddedSize);

		myCartToPolar = essentia::standard::AlgorithmFactory::create("CartesianToPolar");
	}

	void release()
	{
		delete myWindowing;   myWindowing = nullptr;
		delete myFFT;         myFFT = nullptr;
		delete myCartToPolar; myCartToPolar = nullptr;
	}

	/// Append one input timeslice. cookStamp is the INPUT's totalCooks — a
	/// forced re-cook of the consuming CHOP must not write the same
	/// timeslice twice.
	void accumulate(const float* samples, size_t numSamples, int64_t cookStamp)
	{
		if (!samples || numSamples == 0) return;
		if (cookStamp == myLastInputCook) return;
		myAudioRing.write(samples, numSamples);
		myLastInputCook = cookStamp;
		myLastTimeslice = numSamples;
	}

	bool frameReady() const
	{
		return myFftSize > 0 &&
		       myAudioRing.available() >= static_cast<size_t>(myFftSize);
	}

	/// Analyze the latest full window. Returns false while accumulating
	/// (magnitude/phase keep their previous values). frameReady() MUST gate
	/// the read: RingBuffer's vector readLatest front-zero-pads a short
	/// window, and that gating discontinuity corrupts every downstream
	/// spectral descriptor (440 Hz sine -> centroid 600-2258 Hz, measured).
	bool processLatest()
	{
		if (!frameReady())
			return false;

		myAudioRing.readLatest(myAudioFrame, static_cast<size_t>(myFftSize));

		try
		{
			if (myWindowing)
			{
				myWindowing->input("frame").set(myAudioFrame);
				myWindowing->output("frame").set(myWindowedFrame);
				myWindowing->compute();
			}
			if (myFFT)
			{
				myFFT->input("frame").set(myWindowedFrame);
				myFFT->output("fft").set(myFftBuf);
				myFFT->compute();
			}
			if (myCartToPolar)
			{
				myCartToPolar->input("complex").set(myFftBuf);
				// Linear magnitude — exact parity with BatchFrameProcessor.
				myCartToPolar->output("magnitude").set(mySpectrumMag);
				myCartToPolar->output("phase").set(myPhase);
				myCartToPolar->compute();
			}
		}
		catch (...)
		{
			std::fill(mySpectrumMag.begin(), mySpectrumMag.end(), 0.0f);
			std::fill(myPhase.begin(), myPhase.end(), 0.0f);
		}
		return true;
	}

	std::string accumulatingWarning() const
	{
		return "Accumulating audio... (" +
			std::to_string(myAudioRing.available()) + "/" +
			std::to_string(myFftSize) + " samples)";
	}

	/// Non-empty when the input timeslice exceeds one analysis window: the
	/// ring holds only the window, so the excess is NEVER analyzed (measured:
	/// 35-68% of audio skipped at 30 fps with 512/1024 windows, onset F1
	/// collapse — diag/fft-resolution). Empty until a timeslice arrives.
	std::string coverageWarning() const
	{
		if (myFftSize <= 0 || myLastTimeslice <= static_cast<size_t>(myFftSize))
			return std::string();
		return "Window " + std::to_string(myFftSize) + " < input timeslice " +
			std::to_string(myLastTimeslice) + " samples: " +
			std::to_string(myLastTimeslice - static_cast<size_t>(myFftSize)) +
			" samples/cook never analyzed. Raise Window Size above "
			"sampleRate/fps, or raise TD's fps";
	}

	/// Drop accumulated audio + cook stamp (call when the input's identity
	/// changes so stale samples never blend into the next window).
	void reset()
	{
		myAudioRing.clear();
		myLastInputCook = -1;
		myLastTimeslice = 0;
	}

	const std::vector<essentia::Real>& magnitude() const { return mySpectrumMag; }
	const std::vector<essentia::Real>& phase()     const { return myPhase; }
	int specBins() const { return mySpecBins; }
	int fftSize()  const { return myFftSize; }

private:
	int     myFftSize  = 0;
	int     mySpecBins = 0;
	int64_t myLastInputCook = -1;
	size_t  myLastTimeslice = 0;   ///< samples in the most recent accumulate()

	RingBuffer myAudioRing;

	essentia::standard::Algorithm* myWindowing   = nullptr;
	essentia::standard::Algorithm* myFFT         = nullptr;
	essentia::standard::Algorithm* myCartToPolar = nullptr;

	std::vector<essentia::Real> myAudioFrame;
	std::vector<essentia::Real> myWindowedFrame;
	std::vector<std::complex<essentia::Real>> myFftBuf;
	std::vector<essentia::Real> mySpectrumMag;
	std::vector<essentia::Real> myPhase;
};

} // namespace EssentiaTD
