// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaRhythmCHOP.h"
#include "Shared/Utils.h"
#include "Shared/BatchFrameProcessor.h"

#include <essentia/essentiamath.h>
#include <essentia/utils/tnt/tnt_array2d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// 24 mel-spaced bands (26 edges) — compatible with 512+ FFT at 44100+ Hz
// (at 512 the narrowest band still spans two bins; below ~32 kHz the 16 kHz
// top edge exceeds Nyquist and TriangularBands throws — caught at config).
// The TriangularBands default (139 bands) requires far more bins than a
// typical 1024-point FFT provides, so we supply an explicit filterbank.
static const std::vector<Real> kSuperFluxBands = {
	27.5f, 124.6f, 234.8f, 359.6f, 501.1f, 661.5f, 843.3f, 1049.4f,
	1283.0f, 1547.8f, 1847.9f, 2188.2f, 2573.8f, 3011.0f, 3506.6f,
	4068.3f, 4705.0f, 5426.8f, 6244.9f, 7172.4f, 8223.6f, 9415.2f,
	10766.0f, 12297.1f, 14032.7f, 16000.0f
};

// ---------------------------------------------------------------------------
// Channel name table
// ---------------------------------------------------------------------------

static const char* const kChannelNames[EssentiaRhythmCHOP::kNumOutputChannels] = {
	"onset",
	"onset_strength",
	"bpm",
	"beat",
	"beat_phase",
	"beat_confidence",
};

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

EssentiaRhythmCHOP::EssentiaRhythmCHOP(const OP_NodeInfo* info)
	: UnifiedCHOPBase<EssentiaRhythmCHOP>(info)
{
	myOnsetHistory.resize(kOnsetHistorySize, 0.0f);
}

EssentiaRhythmCHOP::~EssentiaRhythmCHOP()
{
	releaseAlgorithms();
}

// ---------------------------------------------------------------------------
// getOutputInfoImpl
// ---------------------------------------------------------------------------

bool EssentiaRhythmCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                             const OP_Inputs* inputs,
                                             bool isBatch)
{
	// Mode-gated params (Windowtype is shared — RT runs its own FFT)
	inputs->enablePar(RhythmmethodName,      isBatch);
	inputs->enablePar(BatchFftsizeName,      isBatch);
	inputs->enablePar(BatchHopsizeName,      isBatch);
	inputs->enablePar(BatchWindowtypeName,   true);
	inputs->enablePar(BatchZeropaddingName,  isBatch);
	inputs->enablePar(RtwindowsizeName,      !isBatch);

	info->numChannels = kNumOutputChannels;

	if (isBatch)
	{
		info->numSamples = myHasResults ? myCachedNumFrames : 1;
		info->startIndex = 0;
		info->sampleRate = myHasResults ? myCachedSampleRate : 1.0f;
	}
	else
	{
		info->numSamples = 1;
		info->sampleRate = static_cast<float>(sanitizedCookRate(inputs));
	}

	return true;
}

// ---------------------------------------------------------------------------
// getChannelNameImpl
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::getChannelNameImpl(int32_t index,
                                              OP_String* name,
                                              const OP_Inputs* /*inputs*/)
{
	if (index >= 0 && index < kNumOutputChannels)
		name->setString(kChannelNames[index]);
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------
// executeRealtimeImpl
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::executeRealtimeImpl(CHOP_Output* output,
                                               const OP_Inputs* inputs)
{
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		zeroOutput(output);
		return;
	}

	// Reset per-frame trigger outputs
	myOutOnset = 0.0f;
	myOutBeat  = 0.0f;

	// ---- Read parameters ----
	const int   onsetMethodIdx = ParametersRhythm::evalOnsetmethod(inputs);
	const float sensitivity    = ParametersRhythm::evalOnsetsensitivity(inputs);
	const int   bpmMin         = ParametersRhythm::evalBpmmin(inputs);
	int         bpmMax         = ParametersRhythm::evalBpmmax(inputs);
	if (bpmMax <= bpmMin) bpmMax = bpmMin + 1;

	const int         rtWindow = ParametersRhythm::evalRtwindowsize(inputs);
	const std::string winType  = evalBatchWindowtype(inputs);

	// Get audio input — v2.0: this op analyzes raw audio and runs its own FFT
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1 || chopIn->numSamples < 1)
	{
		myError = "No audio input connected";
		zeroOutput(output);
		return;
	}
	if (const char* contract = spectrumInputContractError(chopIn))
	{
		myError = contract;
		zeroOutput(output);
		return;
	}

	double sampleRate = chopIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	if (chopIn->numChannels > 1)
		addWarning(WarnMultichannel, "Analyzing channel 0 only");

	// ---- Reconfigure internal FFT if window or window type changed ----
	// No failure-state recording here: every menu combination is a valid
	// Windowing/FFT config, so a throw means something deeper is broken.
	if (rtWindow != myRtFftSize || winType != myRtWindowType)
	{
		try
		{
			myFrameProc.configure(rtWindow, winType, 0);  // RT: no zero-padding
			myRtFftSize    = rtWindow;
			myRtWindowType = winType;
		}
		catch (const std::exception& e)
		{
			myError = std::string("FFT config failed: ") + e.what();
			myFrameProc.release();
			zeroOutput(output);
			return;
		}
		catch (...)
		{
			myError = "FFT config failed with unknown error";
			myFrameProc.release();
			zeroOutput(output);
			return;
		}
	}

	myFrameProc.accumulate(chopIn->getChannelData(0),
	                       static_cast<size_t>(chopIn->numSamples),
	                       chopIn->totalCooks);

	// Empty unless the timeslice exceeds the window (audio being skipped)
	addWarning(WarnResolution, myFrameProc.coverageWarning());

	if (!myFrameProc.processLatest())
	{
		// Warming up (~rtWindow/sampleRate seconds). Freeze the whole pipeline:
		// the onset history and the beat-phase time base must stay 1:1, so
		// neither pushOnsetStrength nor updateBeatPhase may run without the
		// other — advancing either alone skews the tick-time conversion
		// (bufferStartTime) for the life of the buffer.
		addWarning(WarnTransient, myFrameProc.accumulatingWarning());
		zeroOutput(output);
		return;
	}

	const int specBins = myFrameProc.specBins();

	// ---- Reconfigure onset algorithms if bins, method, or rate changed ----
	if (specBins != mySpecSize
	    || onsetMethodIdx != myCurrentMethodIdx
	    || sampleRate != mySampleRate)
	{
		try
		{
			configureOnsetDetection(specBins, onsetMethodIdx, sampleRate);
		}
		catch (const std::exception& e)
		{
			myError = std::string("Algorithm config failed: ") + e.what();
			releaseAlgorithms();
		}
		catch (...)
		{
			myError = "Algorithm config failed with unknown error";
			releaseAlgorithms();
		}
		// Record the attempted config even on failure — otherwise the guard
		// above re-detects a mismatch and retries the throwing construction
		// every single frame with no recovery path
		mySpecSize         = specBins;
		mySampleRate       = sampleRate;
		myCurrentMethodIdx = onsetMethodIdx;
	}

	// ---- Compute onset strength ----
	float odfValue = 0.0f;

	if (onsetMethodIdx == 5) // SuperFlux
	{
		// TriangularBands → band energies
		// mySpectrumBuf is bound once in configureOnsetDetection — copy into
		// it, never reassign
		std::copy(myFrameProc.magnitude().begin(), myFrameProc.magnitude().end(),
		          mySpectrumBuf.begin());
		if (myTriangularBands)
		{
			try
			{
				myTriangularBands->compute();
				// Push to rolling buffer
				myBandsHistory.push_back(myBandsBuf);
				if (static_cast<int>(myBandsHistory.size()) > 3)
					myBandsHistory.pop_front();

				// SuperFluxNovelty (needs >= 3 frames of history)
				if (static_cast<int>(myBandsHistory.size()) >= 3 && mySuperFluxNovelty)
				{
					// Reuse member matrix — assign reuses inner buffers across cooks
					myBandsMatrix.assign(myBandsHistory.begin(), myBandsHistory.end());
					Real noveltyValue = 0.0f;
					mySuperFluxNovelty->input("bands").set(myBandsMatrix);
					mySuperFluxNovelty->output("differences").set(noveltyValue);
					mySuperFluxNovelty->compute();
					odfValue = static_cast<float>(noveltyValue);
				}
			}
			catch (const std::exception& e)
			{
				myError = std::string("SuperFlux error: ") + e.what();
			}
		}
	}
	else // OnsetDetection methods (0-4) — real phase from the internal FFT
	{
		std::copy(myFrameProc.magnitude().begin(), myFrameProc.magnitude().end(),
		          mySpectrumBuf.begin());
		std::copy(myFrameProc.phase().begin(), myFrameProc.phase().end(),
		          myPhaseBuf.begin());

		if (myOnsetDetection)
		{
			try
			{
				myOnsetDetection->compute();
				odfValue = static_cast<float>(myOnsetValue);
			}
			catch (const std::exception& e)
			{
				myError = std::string("OnsetDetection error: ") + e.what();
				writeOutputs(output);
				return;
			}
		}
	}

	myOutOnsetStrength = odfValue;

	const double frameRate = sanitizedFrameRate(inputs);

	// ---- Onset trigger (Onsets-style adaptive threshold) ----
	myOutOnset = detectOnset(odfValue, sensitivity, frameRate) ? 1.0f : 0.0f;

	// ---- Push onset strength into history buffer (for TempoTapDegara) ----
	pushOnsetStrength(odfValue);

	// ---- BPM estimation via TempoTapDegara (periodic) ----
	updateTempoEstimate(frameRate, bpmMin, bpmMax);

	// ---- Beat phase (anchored to ticks) ----
	updateBeatPhase(frameRate, bpmMin, bpmMax);

	// ---- Commit outputs ----
	writeOutputs(output);
}

// ---------------------------------------------------------------------------
// snapshotAndLaunch
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::snapshotAndLaunch(AudioSnapshot audio,
                                             const OP_Inputs* inputs)
{
	BatchRhythmParams params;
	params.fftSize       = evalBatchFftsize(inputs);
	params.hopSize       = evalBatchHopsize(inputs);
	params.windowType    = evalBatchWindowtype(inputs);
	params.zeroPad       = evalBatchZeropadding(inputs);
	params.rhythmMethod  = ParametersRhythm::evalRhythmmethod(inputs);
	params.onsetMethodIdx = ParametersRhythm::evalOnsetmethod(inputs);
	params.sensitivity   = ParametersRhythm::evalOnsetsensitivity(inputs);
	params.bpmMin        = ParametersRhythm::evalBpmmin(inputs);
	params.bpmMax        = ParametersRhythm::evalBpmmax(inputs);
	if (params.bpmMax <= params.bpmMin)
		params.bpmMax = params.bpmMin + 1;

	myRunner.launch([snap = std::move(audio), params]
		(const std::atomic<bool>& cancelFlag, std::atomic<float>& progress)
	{
		return computeBatchAsync(snap, params, cancelFlag, progress);
	});
}

// ---------------------------------------------------------------------------
// onResultCollected
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::onResultCollected(AsyncBatchResult& result)
{
	myDetectedBpm  = result.detectedBpm;
	myUsedAutocorr = result.usedAutocorr;
}

// ---------------------------------------------------------------------------
// setupParametersImpl
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::setupParametersImpl(OP_ParameterManager* manager)
{
	ParametersRhythm::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP
// ---------------------------------------------------------------------------

int32_t EssentiaRhythmCHOP::getNumInfoCHOPChansImpl()
{
	return 6;
}

void EssentiaRhythmCHOP::getInfoCHOPChanImpl(int32_t index,
                                               OP_InfoCHOPChan* chan)
{
	switch (index)
	{
	case 0:
		chan->name->setString("onset_buffer_fill");
		chan->value = static_cast<float>(myOnsetFillCount);
		break;
	case 1:
		chan->name->setString("current_bpm");
		chan->value = myCurrentBpm;
		break;
	case 2:
		chan->name->setString("detected_bpm");
		chan->value = myDetectedBpm;
		break;
	case 3:
		chan->name->setString("used_autocorr");
		chan->value = myUsedAutocorr ? 1.0f : 0.0f;
		break;
	case 4:
		chan->name->setString("computing");
		chan->value = myRunner.isComputing() ? 1.0f : 0.0f;
		break;
	case 5:
		chan->name->setString("progress");
		chan->value = myRunner.progress();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// RT: algorithm lifecycle
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::configureOnsetDetection(int specSize,
                                                   int onsetMethodIdx,
                                                   double sampleRate)
{
	releaseAlgorithms();

	mySpectrumBuf.resize(specSize, 0.0f);
	myPhaseBuf.resize(specSize, 0.0f);

	// Reset threshold state on reconfigure
	myOdfLookback.clear();
	myDecayThreshold  = 0.0f;
	myOdfRunningPeak  = 0.0f;

	// A method/size/rate change alters the ODF scale — stale onset history
	// and ticks would feed TempoTapDegara mixed-scale data for up to ~8.5 s
	std::fill(myOnsetHistory.begin(), myOnsetHistory.end(), 0.0f);
	myOnsetWritePos  = 0;
	myOnsetFillCount = 0;
	myLastTicks.clear();

	if (onsetMethodIdx == 5) // SuperFlux
	{
		myTriangularBands = AlgorithmFactory::create("TriangularBands",
			"inputSize",       specSize,
			"sampleRate",      static_cast<Real>(sampleRate),
			"frequencyBands",  kSuperFluxBands);

		// Bind persistent buffers once
		myTriangularBands->input("spectrum").set(mySpectrumBuf);
		myTriangularBands->output("bands").set(myBandsBuf);

		mySuperFluxNovelty = AlgorithmFactory::create("SuperFluxNovelty",
			"binWidth",   3,
			"frameWidth", 2);

		myBandsHistory.clear();
	}
	else // OnsetDetection (hfc, complex, flux, melflux, rms)
	{
		static const char* kMethodNames[] = { "hfc", "complex", "flux", "melflux", "rms" };
		const char* method = kMethodNames[std::clamp(onsetMethodIdx, 0, 4)];

		myOnsetDetection = AlgorithmFactory::create("OnsetDetection",
			"method",     std::string(method),
			"sampleRate", static_cast<Real>(sampleRate));

		// Bind persistent buffers once (Step 9 fix)
		myOnsetDetection->input("spectrum").set(mySpectrumBuf);
		myOnsetDetection->input("phase").set(myPhaseBuf);
		myOnsetDetection->output("onsetDetection").set(myOnsetValue);
	}
}

void EssentiaRhythmCHOP::releaseAlgorithms()
{
	delete myOnsetDetection;   myOnsetDetection   = nullptr;
	delete myTriangularBands;  myTriangularBands  = nullptr;
	delete mySuperFluxNovelty; mySuperFluxNovelty = nullptr;
	myBandsHistory.clear();
}

// ---------------------------------------------------------------------------
// RT: onset history
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::pushOnsetStrength(float value)
{
	myOnsetHistory[myOnsetWritePos] = value;
	myOnsetWritePos = (myOnsetWritePos + 1) % kOnsetHistorySize;
	if (myOnsetFillCount < kOnsetHistorySize)
		++myOnsetFillCount;
}

// ---------------------------------------------------------------------------
// RT: Onsets-style adaptive onset detection
// ---------------------------------------------------------------------------

bool EssentiaRhythmCHOP::detectOnset(float odfValue, float sensitivity, double frameRate)
{
	// Frame-rate-independent lookback window (frames) and decay factor derived
	// from the time-based constants and the sanitized frame rate.
	const int lookbackWindow = std::max(1,
		static_cast<int>(std::lround(kThresholdDelaySeconds * frameRate)));
	const float decayFactor =
		std::exp(-1.0f / (kDecayTimeConstant * static_cast<float>(frameRate)));

	// Append current value to the lookback ring, trimmed to the window length.
	auto pushLookback = [&](float v)
	{
		myOdfLookback.push_back(v);
		while (static_cast<int>(myOdfLookback.size()) > lookbackWindow)
			myOdfLookback.pop_front();
	};

	// Track the signal's own ODF scale (slow-decaying peak)
	myOdfRunningPeak = std::max(odfValue, myOdfRunningPeak * 0.999f);

	// Silence gate — relative to the running peak so it works for any onset
	// method's native ODF scale, with an absolute floor for digital silence
	if (odfValue < std::max(kSilenceFloor, kSilenceRelative * myOdfRunningPeak))
	{
		myDecayThreshold *= decayFactor;
		pushLookback(odfValue);
		return false;
	}

	// Running mean over lookback window — computed from prior samples only, so
	// the current sample is excluded (it must stand out against its recent past).
	const bool  hadHistory = !myOdfLookback.empty();
	float lookbackMean = 0.0f;
	if (hadHistory)
	{
		float lookbackSum = 0.0f;
		for (float v : myOdfLookback) lookbackSum += v;
		lookbackMean = lookbackSum / static_cast<float>(myOdfLookback.size());
	}
	pushLookback(odfValue);

	// Adaptive threshold: must exceed mean by a factor controlled by sensitivity
	// sensitivity=1 → factor 1.0 (fires often), sensitivity=0 → factor 4.0 (rarely fires)
	const float factor = 1.0f + 3.0f * (1.0f - sensitivity);
	const float meanThreshold = lookbackMean * factor;

	// Must also exceed decaying peak threshold (prevents echo/reverb double-triggers).
	// Suppress triggers until at least one prior sample exists, otherwise an empty
	// lookback (mean 0) would fire on the first non-silent frame after a reconfigure.
	bool isOnset = hadHistory
		&& (odfValue > meanThreshold) && (odfValue > myDecayThreshold);

	// Update decay: raise on onset to suppress immediate re-trigger, otherwise decay
	if (isOnset)
		myDecayThreshold = odfValue * 1.1f;
	else
		myDecayThreshold *= decayFactor;

	return isOnset;
}

// ---------------------------------------------------------------------------
// RT: TempoTapDegara BPM estimation (periodic)
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::updateTempoEstimate(double frameRate,
                                               int bpmMin, int bpmMax)
{
	const int updateInterval = std::max(1,
		static_cast<int>(std::lround(kTempoUpdateSeconds * frameRate)));
	++myTempoUpdateCounter;
	if (myTempoUpdateCounter < updateInterval)
		return;
	myTempoUpdateCounter = 0;

	// Need ~2 seconds of history minimum
	if (myOnsetFillCount < static_cast<int>(2.0 * frameRate))
		return;

	// Build contiguous ODF vector from circular buffer (oldest first)
	const int histLen = std::min(myOnsetFillCount, kOnsetHistorySize);
	std::vector<Real> odfVector(histLen);
	int readStart = (myOnsetFillCount < kOnsetHistorySize) ? 0 : myOnsetWritePos;
	for (int i = 0; i < histLen; ++i)
		odfVector[i] = std::max(0.0f, myOnsetHistory[(readStart + i) % kOnsetHistorySize]);

	try
	{
		// Clamp to TempoTapDegara's valid ranges
		int ttMinTempo = std::clamp(bpmMin, 40, 180);
		int ttMaxTempo = std::clamp(bpmMax, 60, 250);
		if (ttMaxTempo - ttMinTempo < 20)
			ttMaxTempo = std::min(250, ttMinTempo + 20);

		std::unique_ptr<Algorithm> tempotap(AlgorithmFactory::create("TempoTapDegara",
			"sampleRateODF", static_cast<Real>(frameRate),
			"resample",      std::string("x2"),
			"minTempo",      ttMinTempo,
			"maxTempo",      ttMaxTempo));

		std::vector<Real> ticks;
		tempotap->input("onsetDetections").set(odfVector);
		tempotap->output("ticks").set(ticks);
		tempotap->compute();

		if (ticks.size() >= 2)
		{
			// Convert tick times (relative to buffer start) to absolute times
			const double bufferDuration = static_cast<double>(histLen) / frameRate;
			const double bufferStartTime = myAccumulatedTime - bufferDuration;

			myLastTicks.resize(ticks.size());
			for (size_t i = 0; i < ticks.size(); ++i)
				myLastTicks[i] = bufferStartTime + static_cast<double>(ticks[i]);

			// BPM from median tick interval
			std::vector<float> intervals;
			for (size_t i = 1; i < ticks.size(); ++i)
			{
				float interval = static_cast<float>(ticks[i] - ticks[i - 1]);
				if (interval > 0.0f)
					intervals.push_back(interval);
			}
			if (!intervals.empty())
			{
				std::sort(intervals.begin(), intervals.end());
				float medianInterval = intervals[intervals.size() / 2];
				if (medianInterval > 0.0f)
					myCurrentBpm = std::clamp(60.0f / medianInterval,
					                          static_cast<float>(bpmMin),
					                          static_cast<float>(bpmMax));
			}

			// Confidence from tick interval regularity
			if (intervals.size() >= 2)
			{
				float mean = std::accumulate(intervals.begin(), intervals.end(), 0.0f)
				             / static_cast<float>(intervals.size());
				float variance = 0.0f;
				for (float iv : intervals)
					variance += (iv - mean) * (iv - mean);
				variance /= static_cast<float>(intervals.size());
				float cv = (mean > 0.0f) ? std::sqrt(variance) / mean : 1.0f;
				myBeatConfidence = std::clamp(1.0f - cv * 2.0f, 0.0f, 1.0f);
			}
		}
	}
	catch (...) { /* keep existing BPM if TempoTapDegara fails */ }
}

// ---------------------------------------------------------------------------
// RT: frame-rate sanitization
// ---------------------------------------------------------------------------

double EssentiaRhythmCHOP::sanitizedFrameRate(const OP_Inputs* inputs)
{
	const OP_TimeInfo* ti = inputs->getTimeInfo();
	if (!ti) return 60.0;
	const double rate = ti->rate;
	return (std::isfinite(rate) && rate >= 1.0 && rate <= 1000.0) ? rate : 60.0;
}

// ---------------------------------------------------------------------------
// RT: beat phase (anchored to TempoTapDegara ticks)
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::updateBeatPhase(double frameRate,
                                           int bpmMin, int bpmMax)
{
	const float clampedBpm = std::clamp(myCurrentBpm,
	                                    static_cast<float>(bpmMin),
	                                    static_cast<float>(bpmMax));
	const double dt = 1.0 / frameRate; // frameRate pre-sanitized by caller
	myAccumulatedTime += dt;

	// Self-heal a poisoned time base — with a broken accumulator every tick
	// comparison degenerates and the phase pins to 0
	if (!std::isfinite(myAccumulatedTime) || myAccumulatedTime < 0.0)
	{
		myAccumulatedTime = 0.0;
		myLastTicks.clear();
	}

	// Default: free-running phase advance from BPM
	double phase = myBeatPhase + clampedBpm * dt / 60.0;

	// Anchor to TempoTapDegara ticks when available
	if (myLastTicks.size() >= 2)
	{
		double lastTick = -1.0;
		double nextTick = -1.0;
		for (size_t i = 0; i < myLastTicks.size(); ++i)
		{
			if (myLastTicks[i] <= myAccumulatedTime)
				lastTick = myLastTicks[i];
			else
			{
				nextTick = myLastTicks[i];
				break;
			}
		}
		if (lastTick >= 0.0)
		{
			const double beatPeriod = 60.0 / clampedBpm;
			if (nextTick < 0.0)
			{
				// Past the newest tick — extrapolate the beat grid at the
				// current BPM so the phase keeps wrapping per beat instead
				// of growing without bound
				phase = (myAccumulatedTime - lastTick) / beatPeriod;
			}
			else
			{
				const double span = nextTick - lastTick;
				if (span > 0.001)
					phase = (myAccumulatedTime - lastTick) / span;
			}
		}
	}

	// Wrap into [0,1); self-heal if the value was ever poisoned (a NaN phase
	// latches forever otherwise — NaN comparisons are all false)
	phase = std::fmod(phase, 1.0);
	if (phase < 0.0) phase += 1.0;
	if (!std::isfinite(phase)) phase = 0.0;

	// Beat edge = phase wrapped backward. Covers all sources of wrap:
	// free-running rollover, crossing a real tick (anchor jumps to the next
	// span), and each extrapolated beat past the newest tick. The absolute
	// anchor value is never >= 1 inside a tick span, so the old
	// ">= 1 then fmod" edge test could not fire there.
	if (phase < static_cast<double>(myBeatPhase) - 0.5)
		myOutBeat = 1.0f;

	myBeatPhase = static_cast<float>(phase);

	myOutBpm            = clampedBpm;
	myOutBeatPhase      = myBeatPhase;
	myOutBeatConfidence = myBeatConfidence;
}

// ---------------------------------------------------------------------------
// RT: write outputs
// ---------------------------------------------------------------------------

void EssentiaRhythmCHOP::writeOutputs(CHOP_Output* output)
{
	if (!output || output->numChannels < kNumOutputChannels)
		return;

	output->channels[0][0] = myOutOnset;
	output->channels[1][0] = myOutOnsetStrength;
	output->channels[2][0] = myOutBpm;
	output->channels[3][0] = myOutBeat;
	output->channels[4][0] = myOutBeatPhase;
	output->channels[5][0] = myOutBeatConfidence;
}

// ---------------------------------------------------------------------------
// Static async batch worker
// ---------------------------------------------------------------------------

AsyncBatchResult EssentiaRhythmCHOP::computeBatchAsync(
	const AudioSnapshot&     audio,
	const BatchRhythmParams& params,
	const std::atomic<bool>& cancelFlag,
	std::atomic<float>&      progress)
{
	AsyncBatchResult result;

	const double sampleRate = (audio.sampleRate > 0.0) ? audio.sampleRate : 44100.0;
	const int audioLength   = audio.numSamples;
	const float* audioData  = audio.data.data();

	const int zeroPad = zeroPadFromFactor(params.zeroPad, params.fftSize);

	// ---- Per-frame onset detection via FFT (with real phase) ----
	BatchFrameProcessor frameProc;
	frameProc.configure(params.fftSize, params.hopSize, params.windowType, zeroPad);
	if (!frameProc.processAllFrames(audioData, audioLength, &cancelFlag))
	{
		result.success = false;
		result.error   = "Cancelled";
		return result;
	}

	const int numFrames = frameProc.numFrames();
	if (numFrames == 0)
	{
		result.warning = "Audio too short for given FFT size";
		result.success = false;
		return result;
	}

	result.warning = batchFramingWarning(params.fftSize, params.hopSize);

	// ---- Compute onset strength per frame ----
	std::vector<float> onsetStrength(numFrames, 0.0f);
	std::vector<float> onsetBinary(numFrames, 0.0f);

	if (params.onsetMethodIdx == 5) // SuperFlux
	{
		// TriangularBands for all frames
		std::unique_ptr<Algorithm> triBands(AlgorithmFactory::create("TriangularBands",
			"inputSize",       frameProc.specBins(),
			"sampleRate",      static_cast<Real>(sampleRate),
			"frequencyBands",  kSuperFluxBands));

		std::vector<std::vector<Real>> allBands(numFrames);
		std::vector<Real> bandsBuf;

		for (int f = 0; f < numFrames; ++f)
		{
			if (cancelFlag.load(std::memory_order_relaxed))
			{ result.success = false; return result; }

			const auto& spectrum = frameProc.getSpectrum(f);
			triBands->input("spectrum").set(spectrum);
			triBands->output("bands").set(bandsBuf);
			triBands->compute();
			allBands[f] = bandsBuf;

			progress.store(0.2f * (f + 1) / static_cast<float>(numFrames),
			               std::memory_order_relaxed);
		}

		// SuperFluxNovelty per frame (needs frameWidth+1 frames of history)
		constexpr int kFrameWidth = 2;
		if (numFrames > kFrameWidth)
		{
			std::unique_ptr<Algorithm> sfNovelty(AlgorithmFactory::create("SuperFluxNovelty",
				"binWidth", 3, "frameWidth", kFrameWidth));

			Real noveltyValue = 0.0f;
			sfNovelty->output("differences").set(noveltyValue);

			for (int f = kFrameWidth; f < numFrames; ++f)
			{
				if (cancelFlag.load(std::memory_order_relaxed))
				{ result.success = false; return result; }

				std::vector<std::vector<Real>> bandsWindow(
					allBands.begin() + (f - kFrameWidth),
					allBands.begin() + f + 1);

				sfNovelty->input("bands").set(bandsWindow);
				sfNovelty->compute();
				onsetStrength[f] = static_cast<float>(noveltyValue);

				progress.store(0.2f + 0.15f * (f + 1) / static_cast<float>(numFrames),
				               std::memory_order_relaxed);
			}
		}

		progress.store(0.35f, std::memory_order_relaxed);
	}
	else // OnsetDetection methods 0-4 (with real phase from BatchFrameProcessor)
	{
		static const char* kOnsetMethods[] = { "hfc", "complex", "flux", "melflux", "rms" };
		const char* onsetMethod = kOnsetMethods[std::clamp(params.onsetMethodIdx, 0, 4)];

		std::unique_ptr<Algorithm> onsetDetection(AlgorithmFactory::create("OnsetDetection",
			"method",     std::string(onsetMethod),
			"sampleRate", static_cast<Real>(sampleRate)));

		Real onsetValue = 0.0f;
		onsetDetection->output("onsetDetection").set(onsetValue);

		for (int f = 0; f < numFrames; ++f)
		{
			if (cancelFlag.load(std::memory_order_relaxed))
			{ result.success = false; return result; }

			const auto& spectrum = frameProc.getSpectrum(f);
			const auto& phase    = frameProc.getPhase(f);
			try {
				onsetDetection->input("spectrum").set(spectrum);
				onsetDetection->input("phase").set(phase);
				onsetDetection->compute();
				onsetStrength[f] = static_cast<float>(onsetValue);
			} catch (...) {}

			progress.store(0.35f * (f + 1) / static_cast<float>(numFrames),
			               std::memory_order_relaxed);
		}
	}

	// ---- Onset thresholding via Essentia's Onsets algorithm (all methods) ----
	try
	{
		TNT::Array2D<Real> detectionMatrix(1, numFrames);
		for (int f = 0; f < numFrames; ++f)
			detectionMatrix[0][f] = static_cast<Real>(onsetStrength[f]);

		std::vector<Real> weights = { 1.0f };

		// Map sensitivity [0,1] → alpha [0.01,0.3]: higher sensitivity = higher alpha = more onsets
		const Real onsetsAlpha = static_cast<Real>(0.01 + 0.29 * params.sensitivity);
		// Map sensitivity [0,1] → silenceThreshold [0.05,0.005]: higher sensitivity = lower threshold
		const Real onsetsSilence = static_cast<Real>(0.05 - 0.045 * params.sensitivity);

		std::unique_ptr<Algorithm> onsets(AlgorithmFactory::create("Onsets",
			"frameRate",        static_cast<Real>(sampleRate / params.hopSize),
			"alpha",            onsetsAlpha,
			"delay",            5,
			"silenceThreshold", onsetsSilence));

		std::vector<Real> onsetTimes;
		onsets->input("detections").set(detectionMatrix);
		onsets->input("weights").set(weights);
		onsets->output("onsets").set(onsetTimes);
		onsets->compute();

		const double secondsPerFrame =
			static_cast<double>(params.hopSize) / sampleRate;
		for (const auto& time : onsetTimes)
		{
			int frameIdx = static_cast<int>(
				std::round(static_cast<double>(time) / secondsPerFrame));
			if (frameIdx >= 0 && frameIdx < numFrames)
				onsetBinary[frameIdx] = 1.0f;
		}
	}
	catch (...)
	{
		// Fallback: simple threshold if Onsets fails
		float runningMax = 1e-4f;
		const float threshold = 1.0f - params.sensitivity;
		for (int f = 0; f < numFrames; ++f)
		{
			runningMax = std::max(runningMax * 0.999f, onsetStrength[f]);
			const float normalised = (runningMax > 1e-6f)
				? (onsetStrength[f] / runningMax) : 0.0f;
			onsetBinary[f] = (normalised >= threshold) ? 1.0f : 0.0f;
		}
	}

	progress.store(0.45f, std::memory_order_relaxed);

	frameProc.release();

	// ---- BPM + beat detection ----
	float detectedBpm    = 0.0f;
	float beatConfidence = 0.0f;
	std::vector<int> beatFrames;
	bool rhythmExtractorOk = false;

	if (cancelFlag.load(std::memory_order_relaxed))
	{ result.success = false; return result; }

	// ---- Resample to 44100 Hz for RhythmExtractor2013 if needed ----
	std::vector<Real> audioSignal(audioLength);
	for (int i = 0; i < audioLength; ++i)
		audioSignal[i] = static_cast<Real>(audioData[i]);

	double rhythmSampleRate = sampleRate;

	if (std::abs(sampleRate - 44100.0) > 1.0)
	{
		try
		{
			std::unique_ptr<Algorithm> resampler(AlgorithmFactory::create("Resample",
				"inputSampleRate",  static_cast<Real>(sampleRate),
				"outputSampleRate", static_cast<Real>(44100.0),
				"quality",          1));

			std::vector<Real> resampledSignal;
			resampler->input("signal").set(audioSignal);
			resampler->output("signal").set(resampledSignal);
			resampler->compute();

			audioSignal = std::move(resampledSignal);
			rhythmSampleRate = 44100.0;
		}
		catch (...)
		{
			result.warning = "Resample to 44100 Hz failed — BPM may be inaccurate";
		}
	}

	progress.store(0.5f, std::memory_order_relaxed);

	// Try RhythmExtractor2013
	{
		static const char* methodNames[] = { "multifeature", "degara" };
		const char* method = methodNames[std::clamp(params.rhythmMethod, 0, 1)];

		Algorithm* rhythmExtractor = nullptr;
		try {
			// Clamp to RhythmExtractor2013's valid ranges: minTempo [40,180], maxTempo [60,250]
			int reMinTempo = std::clamp(params.bpmMin, 40, 180);
			int reMaxTempo = std::clamp(params.bpmMax, 60, 250);
			if (reMaxTempo <= reMinTempo)
				reMaxTempo = std::min(250, reMinTempo + 20);

			rhythmExtractor = AlgorithmFactory::create("RhythmExtractor2013",
				"method",   std::string(method),
				"minTempo", reMinTempo,
				"maxTempo", reMaxTempo);
		}
		catch (const std::exception& e) {
			result.warning = std::string("RhythmExtractor2013 failed: ") + e.what()
				+ " — using autocorrelation fallback";
		}
		catch (...) {
			result.warning = "RhythmExtractor2013 failed — using autocorrelation fallback";
		}

		if (rhythmExtractor)
		{
			try {
				Real bpmOut  = 0.0f;
				Real confOut = 0.0f;
				std::vector<Real> ticks;
				std::vector<Real> estimates;
				std::vector<Real> bpmIntervals;

				rhythmExtractor->input("signal").set(audioSignal);
				rhythmExtractor->output("bpm").set(bpmOut);
				rhythmExtractor->output("ticks").set(ticks);
				rhythmExtractor->output("confidence").set(confOut);
				rhythmExtractor->output("estimates").set(estimates);
				rhythmExtractor->output("bpmIntervals").set(bpmIntervals);

				rhythmExtractor->compute();
				progress.store(0.9f, std::memory_order_relaxed);

				detectedBpm    = static_cast<float>(bpmOut);
				// Degara method doesn't produce confidence — default to 1
				beatConfidence = (params.rhythmMethod == 1)
					? 1.0f : static_cast<float>(confOut);

				const double secondsPerFrame =
					static_cast<double>(params.hopSize) / sampleRate;
				for (const auto& tickSec : ticks)
				{
					const int frameIdx = static_cast<int>(std::round(
						static_cast<double>(tickSec) / secondsPerFrame));
					if (frameIdx >= 0 && frameIdx < numFrames)
						beatFrames.push_back(frameIdx);
				}

				rhythmExtractorOk = (detectedBpm > 0.0f);
			}
			catch (const std::exception& e) {
				result.warning = std::string("RhythmExtractor2013 compute failed: ")
					+ e.what();
			}
			catch (...) {
				result.warning = "RhythmExtractor2013 compute failed (unknown)";
			}

			delete rhythmExtractor;
		}
	}

	if (cancelFlag.load(std::memory_order_relaxed))
	{ result.success = false; return result; }

	// ---- Fallback: autocorrelation BPM on onset strength curve ----
	if (!rhythmExtractorOk)
	{
		const double framesPerSecond =
			sampleRate / static_cast<double>(params.hopSize);
		const int lagMin = std::max(1,
			static_cast<int>(60.0 * framesPerSecond / params.bpmMax));
		const int lagMax = std::max(lagMin + 1,
			static_cast<int>(60.0 * framesPerSecond / params.bpmMin));

		if (numFrames > lagMax + 1)
		{
			const float mean = std::accumulate(onsetStrength.begin(),
				onsetStrength.end(), 0.0f) / static_cast<float>(numFrames);

			double r0 = 0.0;
			for (int i = 0; i < numFrames; ++i)
			{
				const double v = static_cast<double>(onsetStrength[i] - mean);
				r0 += v * v;
			}

			if (r0 > 1e-6)
			{
				static constexpr int kNumHarmonics = 4;
				const int maxNeededLag =
					std::min(lagMax * kNumHarmonics, numFrames - 1);

				std::vector<double> autocorr(maxNeededLag + 1, 0.0);
				for (int lag = lagMin; lag <= maxNeededLag; ++lag)
				{
					if (cancelFlag.load(std::memory_order_relaxed))
					{ result.success = false; return result; }

					double r = 0.0;
					int    n = 0;
					for (int i = 0; i + lag < numFrames; ++i)
					{
						r += static_cast<double>(onsetStrength[i] - mean)
						   * static_cast<double>(onsetStrength[i + lag] - mean);
						++n;
					}
					if (n > 0) autocorr[lag] = r / static_cast<double>(n);

					progress.store(0.5f + 0.4f * static_cast<float>(lag - lagMin)
						/ static_cast<float>(std::max(1, maxNeededLag - lagMin)),
						std::memory_order_relaxed);
				}

				int    bestLag   = lagMin;
				double bestScore = -1e9;
				for (int lag = lagMin; lag <= lagMax; ++lag)
				{
					double score = 0.0;
					for (int h = 1; h <= kNumHarmonics; ++h)
					{
						const int hLag = h * lag;
						if (hLag <= maxNeededLag)
							score += autocorr[hLag] / static_cast<double>(h);
					}
					if (score > bestScore)
					{
						bestScore = score;
						bestLag   = lag;
					}
				}

				if (bestLag > 0)
				{
					detectedBpm = static_cast<float>(
						60.0 * framesPerSecond / static_cast<double>(bestLag));
					beatConfidence = static_cast<float>(
						std::clamp(bestScore / (r0 / numFrames), 0.0, 1.0));
				}
			}
		}

		// Build beat positions from onset peaks spaced near detected BPM period
		if (detectedBpm > 0.0f)
		{
			const double framesPerSecond2 =
				sampleRate / static_cast<double>(params.hopSize);
			const double beatPeriodFrames =
				60.0 * framesPerSecond2 / detectedBpm;
			const int periodInt =
				std::max(1, static_cast<int>(std::round(beatPeriodFrames)));
			const int halfPeriod = periodInt / 2;

			for (int start = 0; start < numFrames; start += periodInt)
			{
				const int end    = std::min(start + periodInt, numFrames);
				int   bestFrame  = start;
				float bestVal    = onsetStrength[start];
				for (int f = start + 1; f < end; ++f)
				{
					if (onsetStrength[f] > bestVal)
					{
						bestVal   = onsetStrength[f];
						bestFrame = f;
					}
				}
				beatFrames.push_back(bestFrame);
			}

			std::sort(beatFrames.begin(), beatFrames.end());
			std::vector<int> refined;
			for (int i = 0; i < static_cast<int>(beatFrames.size()); ++i)
			{
				if (refined.empty() ||
				    beatFrames[i] - refined.back() >= halfPeriod)
					refined.push_back(beatFrames[i]);
			}
			beatFrames = refined;
		}
	}

	// ---- Build beat binary channel + beat phase ----
	std::vector<float> beatBinary(numFrames, 0.0f);
	std::vector<float> beatPhase(numFrames, 0.0f);

	for (int bf : beatFrames)
		if (bf >= 0 && bf < numFrames)
			beatBinary[bf] = 1.0f;

	if (beatFrames.size() >= 2)
	{
		for (int f = 0; f < beatFrames[0]; ++f)
			beatPhase[f] = 0.0f;

		for (size_t b = 0; b + 1 < beatFrames.size(); ++b)
		{
			const int start = beatFrames[b];
			const int end   = beatFrames[b + 1];
			const int span  = end - start;
			if (span <= 0) continue;
			for (int f = start; f < end; ++f)
				beatPhase[f] = static_cast<float>(f - start)
				             / static_cast<float>(span);
		}

		for (int f = beatFrames.back(); f < numFrames; ++f)
			beatPhase[f] = 0.0f;
	}

	progress.store(0.95f, std::memory_order_relaxed);

	// ---- Pack result ----
	result.cache.assign(kNumOutputChannels, std::vector<float>(numFrames, 0.0f));
	result.cache[0] = onsetBinary;
	result.cache[1] = onsetStrength;
	result.cache[2].assign(numFrames, detectedBpm);
	result.cache[3] = beatBinary;
	result.cache[4] = beatPhase;
	result.cache[5].assign(numFrames, beatConfidence);

	result.numFrames    = numFrames;
	result.sampleRate   = static_cast<float>(sampleRate / params.hopSize);
	result.detectedBpm  = detectedBpm;
	result.usedAutocorr = !rhythmExtractorOk;
	result.success      = true;

	progress.store(1.0f, std::memory_order_relaxed);
	return result;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

UNIFIED_CHOP_DLL_EXPORT(EssentiaRhythmCHOP, "Essentiarhythm", "Essentia Rhythm", "ESR")
