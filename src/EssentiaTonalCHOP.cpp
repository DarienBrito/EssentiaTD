// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaTonalCHOP.h"
#include "Shared/Utils.h"
#include "Shared/BatchFrameProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ===========================================================================
// Channel name builder — static, used by both RT and batch paths
// ===========================================================================

static std::vector<std::string> buildTonalChannelNames(const BatchTonalParams& params)
{
	static const char* noteNames12[] = {
		"c", "cs", "d", "ds", "e", "f", "fs", "g", "gs", "a", "as", "b"
	};

	std::vector<std::string> names;

	if (params.enablePitch)
	{
		names.emplace_back("pitch");
		names.emplace_back("pitch_confidence");
		if (params.enablePitchNote)
			names.emplace_back(params.musicalLabels ? "note" : "pitch_note");
	}

	if (params.enableHpcp)
	{
		if (!params.musicalLabels)
		{
			for (int i = 0; i < params.hpcpSize; ++i)
				names.push_back("hpcp" + std::to_string(i));
		}
		else if (params.hpcpSize == 12)
		{
			for (int i = 0; i < 12; ++i)
				names.push_back(std::string("note_") + noteNames12[i]);
		}
		else if (params.hpcpSize == 24)
		{
			static const char* suffixes[] = { "", "+" };
			for (int semi = 0; semi < 12; ++semi)
				for (int sub = 0; sub < 2; ++sub)
					names.push_back(std::string("note_") + noteNames12[semi] + suffixes[sub]);
		}
		else if (params.hpcpSize == 36)
		{
			static const char* suffixes[] = { "", "~", "+" };
			for (int semi = 0; semi < 12; ++semi)
				for (int sub = 0; sub < 3; ++sub)
					names.push_back(std::string("note_") + noteNames12[semi] + suffixes[sub]);
		}
		else
		{
			for (int i = 0; i < params.hpcpSize; ++i)
				names.push_back("hpcp" + std::to_string(i));
		}
	}

	if (params.enableKey)
	{
		names.emplace_back("key");
		names.emplace_back(params.musicalLabels ? "major_minor" : "key_scale");
		names.emplace_back("key_strength");
	}

	if (params.enableDiss)
		names.emplace_back("dissonance");

	if (params.enableInharm)
		names.emplace_back("inharmonicity");

	return names;
}

// ===========================================================================
// Construction / Destruction
// ===========================================================================

EssentiaTonalCHOP::EssentiaTonalCHOP(const TD::OP_NodeInfo* info)
	: UnifiedCHOPBase<EssentiaTonalCHOP>(info)
{
}

EssentiaTonalCHOP::~EssentiaTonalCHOP()
{
	releaseAlgorithms();
}

// ===========================================================================
// CRTP hooks
// ===========================================================================

bool EssentiaTonalCHOP::getOutputInfoImpl(CHOP_OutputInfo* info,
                                           const OP_Inputs* inputs,
                                           bool isBatch)
{
	const bool pitch         = ParametersTonal::evalEnablepitch(inputs);
	const bool hpcp          = ParametersTonal::evalEnablehpcp(inputs);
	const bool key           = ParametersTonal::evalEnablekey(inputs);
	const bool dissonance    = ParametersTonal::evalEnabledissonance(inputs);
	const bool inharmonicity = ParametersTonal::evalEnableinharmonicity(inputs);
	const int  hpcpSize      = ParametersTonal::evalHpcpsize(inputs);
	const bool pitchNote     = ParametersTonal::evalEnablepitchnote(inputs);
	const bool musicalLabels = ParametersTonal::evalMusicallabels(inputs);
	const bool needPeaks     = hpcp || dissonance || inharmonicity || key;
	const int  keyMode       = ParametersTonal::evalKeymode(inputs);

	// Mode-specific parameter visibility
	inputs->enablePar(SmoothingName,      !isBatch);
	inputs->enablePar(KeyframesName,      !isBatch && key);
	inputs->enablePar(KeymodeName,        isBatch && key);
	inputs->enablePar(KeywindowsizeName,  isBatch && key && keyMode == 1);

	// FFT params visible only in batch mode
	inputs->enablePar(BatchFftsizeName,     isBatch);
	inputs->enablePar(BatchHopsizeName,     isBatch);
	inputs->enablePar(BatchWindowtypeName,  isBatch);
	inputs->enablePar(BatchZeropaddingName, isBatch);

	// Feature co-dependencies (shared by both modes)
	inputs->enablePar(HpcpsizeName,         hpcp);
	inputs->enablePar(MusicallabelsName,     hpcp);
	inputs->enablePar(HpcpharmonicsName,     hpcp);
	inputs->enablePar(ReferencefreqName,     hpcp);
	inputs->enablePar(HpcpnonlinearName,     hpcp);
	inputs->enablePar(HpcpnormalizedName,    hpcp);
	inputs->enablePar(PitchalgoName,         pitch);
	inputs->enablePar(EnablepitchnoteName,   pitch);
	inputs->enablePar(PitchminfreqName,      pitch);
	inputs->enablePar(PitchmaxfreqName,      pitch);
	inputs->enablePar(PitchtoleranceName,    pitch);
	inputs->enablePar(KeyprofileName,        key);
	inputs->enablePar(PeakthresholdName,     needPeaks);
	inputs->enablePar(PeakmaxfreqName,       needPeaks);

	// Rebuild channel names from current param state
	rebuildChannelNames(pitch, pitchNote, hpcp, hpcpSize,
	                    key, dissonance, inharmonicity, musicalLabels);

	// Count channels
	int numCh = 0;
	if (pitch)              numCh += 2;
	if (pitchNote && pitch) numCh += 1;
	if (hpcp)               numCh += hpcpSize;
	if (key)                numCh += 3;
	if (dissonance)         numCh += 1;
	if (inharmonicity)      numCh += 1;

	if (isBatch)
	{
		info->numChannels = (numCh > 0) ? numCh : 1;
		info->numSamples  = myHasResults ? myCachedNumFrames : 1;
		info->startIndex  = 0;
		info->sampleRate  = myHasResults ? myCachedSampleRate : 1.0f;
	}
	else
	{
		info->numChannels = (numCh > 0) ? numCh : 1;
		info->numSamples  = 1;
		info->sampleRate  = static_cast<float>(inputs->getTimeInfo()->rate);
	}

	return true;
}

void EssentiaTonalCHOP::getChannelNameImpl(int32_t index,
                                            OP_String* name,
                                            const OP_Inputs*)
{
	if (index >= 0 && index < static_cast<int32_t>(myChannelNames.size()))
		name->setString(myChannelNames[index].c_str());
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------
// Real-time execution
// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::executeRealtimeImpl(CHOP_Output* output,
                                             const OP_Inputs* inputs)
{
	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
		return;
	}

	// Read parameters
	const int   pitchAlgoIdx    = ParametersTonal::evalPitchalgo(inputs);
	const int   hpcpSize        = ParametersTonal::evalHpcpsize(inputs);
	const bool  enablePitch     = ParametersTonal::evalEnablepitch(inputs);
	const bool  enableHpcp      = ParametersTonal::evalEnablehpcp(inputs);
	const bool  enableKey       = ParametersTonal::evalEnablekey(inputs);
	const bool  enableDiss      = ParametersTonal::evalEnabledissonance(inputs);
	const bool  enableInharm    = ParametersTonal::evalEnableinharmonicity(inputs);
	const bool  musicalLabels   = ParametersTonal::evalMusicallabels(inputs);
	const bool  enablePitchNote = ParametersTonal::evalEnablepitchnote(inputs);
	const float smoothing       = ParametersTonal::evalSmoothing(inputs);
	const int   keyFrames       = ParametersTonal::evalKeyframes(inputs);
	const int   keyProfile      = ParametersTonal::evalKeyprofile(inputs);
	const float pitchMinFreq    = ParametersTonal::evalPitchminfreq(inputs);
	const float pitchMaxFreq    = ParametersTonal::evalPitchmaxfreq(inputs);
	const float pitchTolerance  = ParametersTonal::evalPitchtolerance(inputs);
	const float peakThreshold   = ParametersTonal::evalPeakthreshold(inputs);
	const float peakMaxFreq     = ParametersTonal::evalPeakmaxfreq(inputs);
	const int   hpcpHarmonics   = ParametersTonal::evalHpcpharmonics(inputs);
	const float referenceFreq   = ParametersTonal::evalReferencefreq(inputs);
	const bool  hpcpNonLinear   = ParametersTonal::evalHpcpnonlinear(inputs);
	const int   hpcpNormalized  = ParametersTonal::evalHpcpnormalized(inputs);

	// Get upstream CHOP input
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1)
	{
		myError = "No input connected — expecting EssentiaSpectrumCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	double sampleRate = chopIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	// Extract spectrum from "spectrum" channel
	std::vector<float> specFloat;
	if (!extractChannelSamples(chopIn, "spectrum", specFloat) || specFloat.empty())
	{
		myWarning = "No spectrum channel found in input — connect EssentiaSpectrumCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	const int specBins = static_cast<int>(specFloat.size());

	// Build desired config
	TonalConfig newCfg;
	newCfg.specBins       = specBins;
	newCfg.hpcpSize       = hpcpSize;
	newCfg.sampleRate     = sampleRate;
	newCfg.pitchAlgo      = pitchAlgoIdx;
	newCfg.pitchMinFreq   = pitchMinFreq;
	newCfg.pitchMaxFreq   = pitchMaxFreq;
	newCfg.pitchTolerance = pitchTolerance;
	newCfg.peakThreshold  = peakThreshold;
	newCfg.peakMaxFreq    = peakMaxFreq;
	newCfg.hpcpHarmonics  = hpcpHarmonics;
	newCfg.referenceFreq  = referenceFreq;
	newCfg.hpcpNonLinear  = hpcpNonLinear;
	newCfg.hpcpNormalized = hpcpNormalized;
	newCfg.keyProfile     = keyProfile;

	// Reconfigure when topology changes
	const bool configChanged =
		specBins        != myTCfg.specBins        ||
		hpcpSize        != myTCfg.hpcpSize        ||
		sampleRate      != myTCfg.sampleRate      ||
		pitchAlgoIdx    != myTCfg.pitchAlgo       ||
		pitchMinFreq    != myTCfg.pitchMinFreq    ||
		pitchMaxFreq    != myTCfg.pitchMaxFreq    ||
		pitchTolerance  != myTCfg.pitchTolerance  ||
		peakThreshold   != myTCfg.peakThreshold   ||
		peakMaxFreq     != myTCfg.peakMaxFreq     ||
		hpcpHarmonics   != myTCfg.hpcpHarmonics   ||
		referenceFreq   != myTCfg.referenceFreq   ||
		hpcpNonLinear   != myTCfg.hpcpNonLinear   ||
		hpcpNormalized  != myTCfg.hpcpNormalized  ||
		keyProfile      != myTCfg.keyProfile      ||
		enablePitch     != myEnablePitch           ||
		enableHpcp      != myEnableHpcp            ||
		enableKey       != myEnableKey             ||
		enableDiss      != myEnableDissonance      ||
		enableInharm    != myEnableInharmonicity   ||
		musicalLabels   != myMusicalLabels         ||
		enablePitchNote != myEnablePitchNote;

	if (configChanged)
	{
		try
		{
			configureAlgorithms(newCfg);
			myTCfg = newCfg;

			myEnablePitch          = enablePitch;
			myEnableHpcp           = enableHpcp;
			myEnableKey            = enableKey;
			myEnableDissonance     = enableDiss;
			myEnableInharmonicity  = enableInharm;
			myMusicalLabels        = musicalLabels;
			myEnablePitchNote      = enablePitchNote;
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
		rebuildChannelNames(enablePitch, enablePitchNote, enableHpcp, hpcpSize,
		                    enableKey, enableDiss, enableInharm, musicalLabels);

		// Clear smoothing state on reconfigure
		mySmoothedPitch         = 0.0f;
		mySmoothedPitchConf     = 0.0f;
		mySmoothedHpcp.clear();
		mySmoothedDissonance    = 0.0f;
		mySmoothedInharmonicity = 0.0f;
		myHpcpAccum.clear();
	}

	// Copy float spectrum to essentia::Real buffer
	mySpectrumBuf.resize(specBins);
	for (int i = 0; i < specBins; ++i)
		mySpectrumBuf[i] = static_cast<Real>(specFloat[i]);

	// =========================================================
	// Pitch — PitchYinFFT
	// =========================================================
	if (enablePitch && myPitchYinFFT)
	{
		try
		{
			myPitchYinFFT->input("spectrum").set(mySpectrumBuf);
			myPitchYinFFT->output("pitch").set(myPitchHz);
			myPitchYinFFT->output("pitchConfidence").set(myPitchConfidence);
			myPitchYinFFT->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("PitchYinFFT error: ") + e.what();
			myPitchHz         = 0.0f;
			myPitchConfidence = 0.0f;
		}
	}
	else if (!enablePitch)
	{
		myPitchHz         = 0.0f;
		myPitchConfidence = 0.0f;
	}

	// EMA smooth pitch outputs
	if (enablePitch && smoothing > 0.0f)
	{
		const float alpha = 1.0f - smoothing;
		mySmoothedPitch     = alpha * myPitchHz        + smoothing * mySmoothedPitch;
		mySmoothedPitchConf = alpha * myPitchConfidence + smoothing * mySmoothedPitchConf;
		myPitchHz          = mySmoothedPitch;
		myPitchConfidence  = mySmoothedPitchConf;
	}

	// =========================================================
	// SpectralPeaks — shared intermediate
	// =========================================================
	const bool needPeaks = enableHpcp || enableDiss || enableInharm || enableKey;

	myPeakFreqs.clear();
	myPeakMags.clear();

	if (needPeaks && mySpectralPeaks)
	{
		try
		{
			mySpectralPeaks->input("spectrum").set(mySpectrumBuf);
			mySpectralPeaks->output("frequencies").set(myPeakFreqs);
			mySpectralPeaks->output("magnitudes").set(myPeakMags);
			mySpectralPeaks->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("SpectralPeaks error: ") + e.what();
			myPeakFreqs.clear();
			myPeakMags.clear();
		}
	}

	// =========================================================
	// HPCP (chroma)
	// =========================================================
	myHpcpBuf.assign(hpcpSize, 0.0f);

	if (enableHpcp && myHpcp && !myPeakFreqs.empty())
	{
		try
		{
			myHpcp->input("frequencies").set(myPeakFreqs);
			myHpcp->input("magnitudes").set(myPeakMags);
			myHpcp->output("hpcp").set(myHpcpBuf);
			myHpcp->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("HPCP error: ") + e.what();
			myHpcpBuf.assign(hpcpSize, 0.0f);
		}
	}

	// EMA smooth HPCP outputs
	if (enableHpcp && smoothing > 0.0f)
	{
		const float alpha = 1.0f - smoothing;
		if (mySmoothedHpcp.size() != myHpcpBuf.size())
			mySmoothedHpcp.assign(myHpcpBuf.begin(), myHpcpBuf.end());
		else
		{
			for (size_t i = 0; i < myHpcpBuf.size(); ++i)
				mySmoothedHpcp[i] = alpha * myHpcpBuf[i] + smoothing * mySmoothedHpcp[i];
		}
		for (size_t i = 0; i < myHpcpBuf.size(); ++i)
			myHpcpBuf[i] = mySmoothedHpcp[i];
	}

	// =========================================================
	// Key (requires HPCP vector as pcp input)
	// =========================================================
	myKeyStr   = "";
	myScaleStr = "";
	myKeyStrength = 0.0f;

	if (enableKey && myKey)
	{
		// Accumulate HPCP frames for averaged Key detection
		myHpcpAccum.push_back(myHpcpBuf.empty()
			? std::vector<Real>(12, 0.0f) : myHpcpBuf);
		while (static_cast<int>(myHpcpAccum.size()) > keyFrames)
			myHpcpAccum.pop_front();

		// Average HPCP frames element-wise
		const size_t hpcpLen = myHpcpAccum.front().size();
		myKeyPcpBuf.assign(hpcpLen, 0.0f);
		for (const auto& frame : myHpcpAccum)
			for (size_t i = 0; i < hpcpLen && i < frame.size(); ++i)
				myKeyPcpBuf[i] += frame[i];
		const float invN = 1.0f / static_cast<float>(myHpcpAccum.size());
		for (size_t i = 0; i < hpcpLen; ++i)
			myKeyPcpBuf[i] *= invN;

		try
		{
			myKey->input("pcp").set(myKeyPcpBuf);
			myKey->output("key").set(myKeyStr);
			myKey->output("scale").set(myScaleStr);
			myKey->output("strength").set(myKeyStrength);
			myKey->output("firstToSecondRelativeStrength").set(myFirstToSecondRelativeStrength);
			myKey->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("Key error: ") + e.what();
			myKeyStr      = "";
			myScaleStr    = "";
			myKeyStrength = 0.0f;
		}
	}

	// =========================================================
	// Dissonance
	// =========================================================
	myDissonanceVal = 0.0f;

	if (enableDiss && myDissonance && !myPeakFreqs.empty())
	{
		try
		{
			myDissonance->input("frequencies").set(myPeakFreqs);
			myDissonance->input("magnitudes").set(myPeakMags);
			myDissonance->output("dissonance").set(myDissonanceVal);
			myDissonance->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("Dissonance error: ") + e.what();
			myDissonanceVal = 0.0f;
		}
	}

	// EMA smooth dissonance
	if (enableDiss && smoothing > 0.0f)
	{
		const float alpha = 1.0f - smoothing;
		mySmoothedDissonance = alpha * myDissonanceVal + smoothing * mySmoothedDissonance;
		myDissonanceVal      = mySmoothedDissonance;
	}

	// =========================================================
	// Inharmonicity
	// =========================================================
	myInharmonicityVal = 0.0f;

	if (enableInharm && myInharmonicity && !myPeakFreqs.empty()
	    && myPeakFreqs[0] > 0.0f)
	{
		try
		{
			myInharmonicity->input("frequencies").set(myPeakFreqs);
			myInharmonicity->input("magnitudes").set(myPeakMags);
			myInharmonicity->output("inharmonicity").set(myInharmonicityVal);
			myInharmonicity->compute();
		}
		catch (const std::exception& e)
		{
			myWarning = std::string("Inharmonicity error: ") + e.what();
			myInharmonicityVal = 0.0f;
		}
	}

	// EMA smooth inharmonicity
	if (enableInharm && smoothing > 0.0f)
	{
		const float alpha = 1.0f - smoothing;
		mySmoothedInharmonicity = alpha * myInharmonicityVal + smoothing * mySmoothedInharmonicity;
		myInharmonicityVal      = mySmoothedInharmonicity;
	}

	// =========================================================
	// Write output channels
	// Channel layout matches rebuildChannelNames() order:
	//   [pitch, pitch_confidence, (pitch_note)]   — if enablePitch
	//   [hpcp0 .. hpcpN]                          — if enableHpcp
	//   [key, key_scale, key_strength]             — if enableKey
	//   [dissonance]                               — if enableDiss
	//   [inharmonicity]                            — if enableInharm
	// =========================================================

	int ch = 0;

	auto safeWrite = [&](float val) {
		if (ch < output->numChannels)
			output->channels[ch][0] = val;
		++ch;
	};

	if (enablePitch)
	{
		safeWrite(static_cast<float>(myPitchHz));
		safeWrite(static_cast<float>(myPitchConfidence));

		if (enablePitchNote)
		{
			float noteClass = -1.0f;
			if (myPitchConfidence > 0.0f && myPitchHz > 0.0f)
			{
				float midi = 12.0f * std::log2(static_cast<float>(myPitchHz) / 440.0f) + 69.0f;
				int nc = static_cast<int>(std::round(midi)) % 12;
				if (nc < 0) nc += 12;
				noteClass = static_cast<float>(nc);
			}
			safeWrite(noteClass);
		}
	}

	if (enableHpcp)
	{
		for (int i = 0; i < hpcpSize; ++i)
		{
			const float v = (i < static_cast<int>(myHpcpBuf.size()))
			                ? static_cast<float>(myHpcpBuf[i]) : 0.0f;
			safeWrite(v);
		}
	}

	if (enableKey)
	{
		safeWrite(encodeKey(myKeyStr));

		float scaleVal = -1.0f;
		if (myScaleStr == "major")      scaleVal = 0.0f;
		else if (myScaleStr == "minor") scaleVal = 1.0f;
		safeWrite(scaleVal);

		safeWrite(static_cast<float>(myKeyStrength));
	}

	if (enableDiss)
		safeWrite(static_cast<float>(myDissonanceVal));

	if (enableInharm)
		safeWrite(static_cast<float>(myInharmonicityVal));
}

// ---------------------------------------------------------------------------
// Batch: snapshot parameters and launch async worker
// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::snapshotAndLaunch(AudioSnapshot audio,
                                           const OP_Inputs* inputs)
{
	const int fftSize       = evalBatchFftsize(inputs);
	const int zeroPadFactor = evalBatchZeropadding(inputs);

	BatchTonalParams params;
	params.fftSize         = fftSize;
	params.hopSize         = evalBatchHopsize(inputs);
	params.windowType      = evalBatchWindowtype(inputs);
	params.zeroPad         = zeroPadFromFactor(zeroPadFactor, fftSize);
	params.enablePitch     = ParametersTonal::evalEnablepitch(inputs);
	params.enablePitchNote = ParametersTonal::evalEnablepitchnote(inputs);
	params.enableHpcp      = ParametersTonal::evalEnablehpcp(inputs);
	params.hpcpSize        = ParametersTonal::evalHpcpsize(inputs);
	params.enableKey       = ParametersTonal::evalEnablekey(inputs);
	params.keyMode         = ParametersTonal::evalKeymode(inputs);
	params.keyWindowSize   = ParametersTonal::evalKeywindowsize(inputs);
	params.enableDiss      = ParametersTonal::evalEnabledissonance(inputs);
	params.enableInharm    = ParametersTonal::evalEnableinharmonicity(inputs);
	params.musicalLabels   = ParametersTonal::evalMusicallabels(inputs);
	params.pitchAlgo       = ParametersTonal::evalPitchalgo(inputs);
	params.pitchMinFreq    = ParametersTonal::evalPitchminfreq(inputs);
	params.pitchMaxFreq    = ParametersTonal::evalPitchmaxfreq(inputs);
	params.pitchTolerance  = ParametersTonal::evalPitchtolerance(inputs);
	params.peakThreshold   = ParametersTonal::evalPeakthreshold(inputs);
	params.peakMaxFreq     = ParametersTonal::evalPeakmaxfreq(inputs);
	params.hpcpHarmonics   = ParametersTonal::evalHpcpharmonics(inputs);
	params.referenceFreq   = ParametersTonal::evalReferencefreq(inputs);
	params.hpcpNonLinear   = ParametersTonal::evalHpcpnonlinear(inputs);
	params.hpcpNormalized  = ParametersTonal::evalHpcpnormalized(inputs);
	params.keyProfile      = ParametersTonal::evalKeyprofile(inputs);

	myRunner.launch([audio = std::move(audio), params]
	                (const std::atomic<bool>& cancel, std::atomic<float>& progress)
	{
		return computeBatchAsync(audio, params, cancel, progress);
	});
}

// ---------------------------------------------------------------------------
// Batch: called after a successful result is collected by the base
// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::onResultCollected(AsyncBatchResult& result)
{
	myChannelNames = std::move(result.channelNames);
}

// ---------------------------------------------------------------------------
// Setup parameters
// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::setupParametersImpl(OP_ParameterManager* manager)
{
	ParametersTonal::setup(manager);
}

// ===========================================================================
// Info CHOP — 4 channels: spec_bins, num_frames, computing, progress
// ===========================================================================

int32_t EssentiaTonalCHOP::getNumInfoCHOPChansImpl()
{
	return 4;
}

void EssentiaTonalCHOP::getInfoCHOPChanImpl(int32_t index,
                                              OP_InfoCHOPChan* chan)
{
	switch (index)
	{
	case 0:
		chan->name->setString("spec_bins");
		chan->value = static_cast<float>(myTCfg.specBins);
		break;
	case 1:
		chan->name->setString("num_frames");
		chan->value = static_cast<float>(myCachedNumFrames);
		break;
	case 2:
		chan->name->setString("computing");
		chan->value = myRunner.isComputing() ? 1.0f : 0.0f;
		break;
	case 3:
		chan->name->setString("progress");
		chan->value = myRunner.progress();
		break;
	}
}

// ===========================================================================
// Private helpers
// ===========================================================================

void EssentiaTonalCHOP::configureAlgorithms(const TonalConfig& cfg)
{
	releaseAlgorithms();

	mySpectrumBuf.resize(cfg.specBins, 0.0f);
	myPeakFreqs.reserve(100);
	myPeakMags.reserve(100);
	myHpcpBuf.resize(cfg.hpcpSize, 0.0f);

	const Real sr = static_cast<Real>(cfg.sampleRate);

	const char* pitchAlgoName = (cfg.pitchAlgo == 1)
		? "PitchYinProbabilistic" : "PitchYinFFT";

	myPitchYinFFT = AlgorithmFactory::create(pitchAlgoName,
		"sampleRate",   sr,
		"minFrequency", static_cast<Real>(cfg.pitchMinFreq),
		"maxFrequency", static_cast<Real>(cfg.pitchMaxFreq),
		"tolerance",    static_cast<Real>(cfg.pitchTolerance));

	mySpectralPeaks = AlgorithmFactory::create("SpectralPeaks",
		"sampleRate",         sr,
		"maxPeaks",           100,
		"orderBy",            std::string("frequency"),
		"magnitudeThreshold", static_cast<Real>(cfg.peakThreshold),
		"maxFrequency",       static_cast<Real>(cfg.peakMaxFreq));

	static const char* hpcpNormNames[] = { "unitMax", "unitSum", "none" };
	const int normIdx = std::clamp(cfg.hpcpNormalized, 0, 2);

	myHpcp = AlgorithmFactory::create("HPCP",
		"size",               cfg.hpcpSize,
		"sampleRate",         sr,
		"harmonics",          cfg.hpcpHarmonics,
		"referenceFrequency", static_cast<Real>(cfg.referenceFreq),
		"nonLinear",          cfg.hpcpNonLinear,
		"normalized",         std::string(hpcpNormNames[normIdx]));

	static const char* profileNames[] = {
		"bgate", "temperley", "krumhansl", "edma", "diatonic", "gomez"
	};
	const int profIdx = std::clamp(cfg.keyProfile, 0, 5);

	myKey = AlgorithmFactory::create("Key",
		"profileType", std::string(profileNames[profIdx]));

	myDissonance     = AlgorithmFactory::create("Dissonance");
	myInharmonicity  = AlgorithmFactory::create("Inharmonicity");
}

void EssentiaTonalCHOP::releaseAlgorithms()
{
	delete myPitchYinFFT;   myPitchYinFFT   = nullptr;
	delete mySpectralPeaks; mySpectralPeaks = nullptr;
	delete myHpcp;          myHpcp          = nullptr;
	delete myKey;           myKey           = nullptr;
	delete myDissonance;    myDissonance    = nullptr;
	delete myInharmonicity; myInharmonicity = nullptr;
}

void EssentiaTonalCHOP::rebuildChannelNames(bool pitch, bool pitchNote,
                                             bool hpcp, int hpcpSz,
                                             bool key, bool dissonance,
                                             bool inharmonicity,
                                             bool musicalLabels)
{
	BatchTonalParams p;
	p.enablePitch     = pitch;
	p.enablePitchNote = pitchNote;
	p.enableHpcp      = hpcp;
	p.hpcpSize        = hpcpSz;
	p.enableKey       = key;
	p.enableDiss      = dissonance;
	p.enableInharm    = inharmonicity;
	p.musicalLabels   = musicalLabels;

	myChannelNames = buildTonalChannelNames(p);
}

// ===========================================================================
// Async batch worker
// ===========================================================================

AsyncBatchResult EssentiaTonalCHOP::computeBatchAsync(
	const AudioSnapshot&     audio,
	const BatchTonalParams&  params,
	const std::atomic<bool>& cancelFlag,
	std::atomic<float>&      progress)
{
	AsyncBatchResult result;

	// Frame processing (windowing + FFT)
	BatchFrameProcessor frameProc;
	frameProc.configure(params.fftSize, params.hopSize,
	                    params.windowType, params.zeroPad);
	frameProc.processAllFrames(audio.data.data(),
	                           static_cast<int>(audio.data.size()));

	const int numFrames = frameProc.numFrames();
	const int specBins  = frameProc.specBins();
	(void)specBins;

	if (numFrames == 0)
	{
		result.warning = "Audio too short for given FFT size";
		result.success = true;
		return result;
	}

	// Create local Essentia algorithm instances
	const Real sr = static_cast<Real>(audio.sampleRate);

	const char* pitchAlgoName = (params.pitchAlgo == 1)
		? "PitchYinProbabilistic" : "PitchYinFFT";

	Algorithm* pitchAlgo = AlgorithmFactory::create(pitchAlgoName,
		"sampleRate",   sr,
		"minFrequency", static_cast<Real>(params.pitchMinFreq),
		"maxFrequency", static_cast<Real>(params.pitchMaxFreq),
		"tolerance",    static_cast<Real>(params.pitchTolerance));

	Algorithm* spectralPeaks = AlgorithmFactory::create("SpectralPeaks",
		"sampleRate",         sr,
		"maxPeaks",           100,
		"orderBy",            std::string("frequency"),
		"magnitudeThreshold", static_cast<Real>(params.peakThreshold),
		"maxFrequency",       static_cast<Real>(params.peakMaxFreq));

	static const char* hpcpNormNames[] = { "unitMax", "unitSum", "none" };
	const int normIdx = std::clamp(params.hpcpNormalized, 0, 2);

	Algorithm* hpcpAlgo = AlgorithmFactory::create("HPCP",
		"size",               params.hpcpSize,
		"sampleRate",         sr,
		"harmonics",          params.hpcpHarmonics,
		"referenceFrequency", static_cast<Real>(params.referenceFreq),
		"nonLinear",          params.hpcpNonLinear,
		"normalized",         std::string(hpcpNormNames[normIdx]));

	static const char* profileNames[] = {
		"bgate", "temperley", "krumhansl", "edma", "diatonic", "gomez"
	};
	const int profIdx = std::clamp(params.keyProfile, 0, 5);

	Algorithm* keyAlgo = AlgorithmFactory::create("Key",
		"profileType", std::string(profileNames[profIdx]));

	Algorithm* dissonanceAlgo    = AlgorithmFactory::create("Dissonance");
	Algorithm* inharmonicityAlgo = AlgorithmFactory::create("Inharmonicity");

	// RAII cleanup of local algorithms
	struct AlgoGuard
	{
		Algorithm* pitch;
		Algorithm* peaks;
		Algorithm* hpcp;
		Algorithm* key;
		Algorithm* diss;
		Algorithm* inharm;
		~AlgoGuard()
		{
			delete pitch;
			delete peaks;
			delete hpcp;
			delete key;
			delete diss;
			delete inharm;
		}
	} guard{ pitchAlgo, spectralPeaks, hpcpAlgo, keyAlgo,
	         dissonanceAlgo, inharmonicityAlgo };

	// Build channel names from params
	std::vector<std::string> channelNames = buildTonalChannelNames(params);
	const int numCh = static_cast<int>(channelNames.size());

	// Allocate result cache
	std::vector<std::vector<float>> cache(numCh,
	                                      std::vector<float>(numFrames, 0.0f));

	// Per-frame working buffers
	std::vector<Real> peakFreqs;
	std::vector<Real> peakMags;
	std::vector<Real> hpcpBuf(params.hpcpSize, 0.0f);
	std::vector<Real> keyPcpBuf;
	Real pitchHz          = 0.0f;
	Real pitchConfidence  = 0.0f;
	Real keyStrength      = 0.0f;
	Real firstToSecond    = 0.0f;
	Real dissonanceVal    = 0.0f;
	Real inharmonicityVal = 0.0f;
	std::string keyStr;
	std::string scaleStr;

	// HPCP accumulators for key detection
	std::vector<std::vector<Real>> allHpcp;
	if (params.enableKey && params.keyMode == 0)
		allHpcp.reserve(numFrames);

	std::deque<std::vector<Real>> hpcpAccum;

	// Process each frame
	for (int f = 0; f < numFrames; ++f)
	{
		if (cancelFlag.load(std::memory_order_relaxed))
		{
			result.success = false;
			result.error   = "Cancelled";
			return result;
		}

		progress.store(static_cast<float>(f) / static_cast<float>(numFrames),
		               std::memory_order_relaxed);

		const auto& spectrum = frameProc.getSpectrum(f);
		int ch = 0;

		// Pitch
		if (params.enablePitch)
		{
			try {
				pitchAlgo->input("spectrum").set(spectrum);
				pitchAlgo->output("pitch").set(pitchHz);
				pitchAlgo->output("pitchConfidence").set(pitchConfidence);
				pitchAlgo->compute();
			} catch (...) { pitchHz = 0.0f; pitchConfidence = 0.0f; }

			cache[ch++][f] = static_cast<float>(pitchHz);
			cache[ch++][f] = static_cast<float>(pitchConfidence);

			if (params.enablePitchNote)
			{
				float noteClass = -1.0f;
				if (pitchConfidence > 0.0f && pitchHz > 0.0f)
				{
					float midi = 12.0f * std::log2(static_cast<float>(pitchHz) / 440.0f) + 69.0f;
					int nc = static_cast<int>(std::round(midi)) % 12;
					if (nc < 0) nc += 12;
					noteClass = static_cast<float>(nc);
				}
				cache[ch++][f] = noteClass;
			}
		}

		// SpectralPeaks (shared by HPCP, Dissonance, Inharmonicity, Key)
		const bool needPeaks = params.enableHpcp || params.enableDiss
		                    || params.enableInharm || params.enableKey;
		peakFreqs.clear();
		peakMags.clear();

		if (needPeaks)
		{
			try {
				spectralPeaks->input("spectrum").set(spectrum);
				spectralPeaks->output("frequencies").set(peakFreqs);
				spectralPeaks->output("magnitudes").set(peakMags);
				spectralPeaks->compute();
			} catch (...) { peakFreqs.clear(); peakMags.clear(); }
		}

		// HPCP
		hpcpBuf.assign(params.hpcpSize, 0.0f);
		if (params.enableHpcp && !peakFreqs.empty())
		{
			try {
				hpcpAlgo->input("frequencies").set(peakFreqs);
				hpcpAlgo->input("magnitudes").set(peakMags);
				hpcpAlgo->output("hpcp").set(hpcpBuf);
				hpcpAlgo->compute();
			} catch (...) { hpcpBuf.assign(params.hpcpSize, 0.0f); }
		}

		if (params.enableHpcp)
		{
			for (int i = 0; i < params.hpcpSize && ch < numCh; ++i)
				cache[ch++][f] = (i < static_cast<int>(hpcpBuf.size()))
				                 ? static_cast<float>(hpcpBuf[i]) : 0.0f;
		}

		// Collect HPCP for key detection
		if (params.enableKey)
		{
			if (params.keyMode == 0) // global
			{
				allHpcp.push_back(hpcpBuf.empty()
					? std::vector<Real>(params.hpcpSize, 0.0f) : hpcpBuf);
			}
			else // windowed
			{
				hpcpAccum.push_back(hpcpBuf.empty()
					? std::vector<Real>(params.hpcpSize, 0.0f) : hpcpBuf);
				while (static_cast<int>(hpcpAccum.size()) > params.keyWindowSize)
					hpcpAccum.pop_front();
			}
		}

		// Key (windowed mode — per-frame)
		if (params.enableKey && params.keyMode == 1)
		{
			const size_t hLen = hpcpAccum.empty() ? 0 : hpcpAccum.front().size();
			keyPcpBuf.assign(hLen, 0.0f);
			for (const auto& frame : hpcpAccum)
				for (size_t i = 0; i < hLen && i < frame.size(); ++i)
					keyPcpBuf[i] += frame[i];
			if (!hpcpAccum.empty())
			{
				float invN = 1.0f / static_cast<float>(hpcpAccum.size());
				for (auto& v : keyPcpBuf) v *= invN;
			}

			try {
				keyAlgo->input("pcp").set(keyPcpBuf);
				keyAlgo->output("key").set(keyStr);
				keyAlgo->output("scale").set(scaleStr);
				keyAlgo->output("strength").set(keyStrength);
				keyAlgo->output("firstToSecondRelativeStrength").set(firstToSecond);
				keyAlgo->compute();
			} catch (...) { keyStr = ""; scaleStr = ""; keyStrength = 0.0f; }

			if (ch < numCh) cache[ch++][f] = encodeKey(keyStr);
			float scaleVal = -1.0f;
			if (scaleStr == "major")      scaleVal = 0.0f;
			else if (scaleStr == "minor") scaleVal = 1.0f;
			if (ch < numCh) cache[ch++][f] = scaleVal;
			if (ch < numCh) cache[ch++][f] = static_cast<float>(keyStrength);
		}
		else if (params.enableKey && params.keyMode == 0)
		{
			// Skip key channels — filled in the global pass below
			ch += 3;
		}

		// Dissonance
		if (params.enableDiss)
		{
			dissonanceVal = 0.0f;
			if (!peakFreqs.empty())
			{
				try {
					dissonanceAlgo->input("frequencies").set(peakFreqs);
					dissonanceAlgo->input("magnitudes").set(peakMags);
					dissonanceAlgo->output("dissonance").set(dissonanceVal);
					dissonanceAlgo->compute();
				} catch (...) { dissonanceVal = 0.0f; }
			}
			if (ch < numCh) cache[ch++][f] = static_cast<float>(dissonanceVal);
		}

		// Inharmonicity
		if (params.enableInharm)
		{
			inharmonicityVal = 0.0f;
			if (!peakFreqs.empty() && peakFreqs[0] > 0.0f)
			{
				try {
					inharmonicityAlgo->input("frequencies").set(peakFreqs);
					inharmonicityAlgo->input("magnitudes").set(peakMags);
					inharmonicityAlgo->output("inharmonicity").set(inharmonicityVal);
					inharmonicityAlgo->compute();
				} catch (...) { inharmonicityVal = 0.0f; }
			}
			if (ch < numCh) cache[ch++][f] = static_cast<float>(inharmonicityVal);
		}
	}

	// Global key mode: compute one key for the entire file
	if (params.enableKey && params.keyMode == 0 && !allHpcp.empty())
	{
		const size_t hLen = allHpcp.front().size();
		keyPcpBuf.assign(hLen, 0.0f);
		for (const auto& frame : allHpcp)
			for (size_t i = 0; i < hLen && i < frame.size(); ++i)
				keyPcpBuf[i] += frame[i];
		float invN = 1.0f / static_cast<float>(allHpcp.size());
		for (auto& v : keyPcpBuf) v *= invN;

		try {
			keyAlgo->input("pcp").set(keyPcpBuf);
			keyAlgo->output("key").set(keyStr);
			keyAlgo->output("scale").set(scaleStr);
			keyAlgo->output("strength").set(keyStrength);
			keyAlgo->output("firstToSecondRelativeStrength").set(firstToSecond);
			keyAlgo->compute();
		} catch (...) { keyStr = ""; scaleStr = ""; keyStrength = 0.0f; }

		const float keyVal  = encodeKey(keyStr);
		float scaleVal      = -1.0f;
		if (scaleStr == "major")      scaleVal = 0.0f;
		else if (scaleStr == "minor") scaleVal = 1.0f;
		const float strengthVal = static_cast<float>(keyStrength);

		// Locate the key channel base index
		int keyChBase = 0;
		if (params.enablePitch)
		{
			keyChBase += 2;
			if (params.enablePitchNote) ++keyChBase;
		}
		if (params.enableHpcp) keyChBase += params.hpcpSize;

		for (int f = 0; f < numFrames; ++f)
		{
			if (keyChBase     < numCh) cache[keyChBase][f]     = keyVal;
			if (keyChBase + 1 < numCh) cache[keyChBase + 1][f] = scaleVal;
			if (keyChBase + 2 < numCh) cache[keyChBase + 2][f] = strengthVal;
		}
	}

	result.cache        = std::move(cache);
	result.channelNames = std::move(channelNames);
	result.numFrames    = numFrames;
	result.sampleRate   = static_cast<float>(audio.sampleRate / params.hopSize);
	result.success      = true;

	progress.store(1.0f, std::memory_order_relaxed);
	return result;
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

UNIFIED_CHOP_DLL_EXPORT(EssentiaTonalCHOP, "Essentiatonal", "Essentia Tonal", "EST")
