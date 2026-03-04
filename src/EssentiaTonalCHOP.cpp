// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EssentiaTonalCHOP.h"
#include "Shared/EssentiaInit.h"
#include "Shared/Utils.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace TD;
using namespace essentia;
using namespace essentia::standard;

namespace EssentiaTD
{

// ===========================================================================
// Construction / Destruction
// ===========================================================================

EssentiaTonalCHOP::EssentiaTonalCHOP(const OP_NodeInfo* /*info*/)
{
	std::string initErr;
	myInitOk = ensureEssentiaInit(initErr);
	if (!myInitOk)
		myError = initErr;
}

EssentiaTonalCHOP::~EssentiaTonalCHOP()
{
	releaseAlgorithms();
}

// ===========================================================================
// TD CHOP overrides
// ===========================================================================

void EssentiaTonalCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo,
                                        const OP_Inputs*, void*)
{
	ginfo->cookEveryFrame      = false;
	ginfo->cookEveryFrameIfAsked = true;
	// Tonal analysis operates on single-frame analysis results, not a
	// continuous audio stream — output 1 sample per channel.
	ginfo->timeslice           = false;
	ginfo->inputMatchIndex     = -1;
}

bool EssentiaTonalCHOP::getOutputInfo(CHOP_OutputInfo* info,
                                       const OP_Inputs* inputs, void*)
{
	const bool pitch         = ParametersTonal::evalEnablepitch(inputs);
	const bool hpcp          = ParametersTonal::evalEnablehpcp(inputs);
	const bool key           = ParametersTonal::evalEnablekey(inputs);
	const bool dissonance    = ParametersTonal::evalEnabledissonance(inputs);
	const bool inharmonicity = ParametersTonal::evalEnableinharmonicity(inputs);
	const int  hpcpSize      = ParametersTonal::evalHpcpsize(inputs);
	const bool pitchNote     = ParametersTonal::evalEnablepitchnote(inputs);

	// Parameter co-dependencies
	inputs->enablePar(HpcpsizeName, hpcp);
	inputs->enablePar(MusicallabelsName, hpcp);
	inputs->enablePar(PitchalgoName, pitch);
	inputs->enablePar(EnablepitchnoteName, pitch);

	int numCh = 0;
	if (pitch)              numCh += 2;          // pitch + pitch_confidence
	if (pitchNote && pitch) numCh += 1;          // pitch_note
	if (hpcp)               numCh += hpcpSize;   // hpcp0..hpcpN
	if (key)                numCh += 3;          // key + key_scale + key_strength
	if (dissonance)         numCh += 1;
	if (inharmonicity)      numCh += 1;

	info->numChannels = (numCh > 0) ? numCh : 1; // TD requires at least 1 channel
	info->numSamples  = 1;
	info->sampleRate  = static_cast<float>(inputs->getTimeInfo()->rate);
	return true;
}

void EssentiaTonalCHOP::getChannelName(int32_t index, OP_String* name,
                                        const OP_Inputs*, void*)
{
	if (index >= 0 && index < static_cast<int32_t>(myChannelNames.size()))
		name->setString(myChannelNames[index].c_str());
	else
		name->setString("unknown");
}

// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::execute(CHOP_Output* output,
                                 const OP_Inputs* inputs, void*)
{
	myError.clear();
	myWarning.clear();

	if (!myInitOk)
	{
		myError = "Essentia failed to initialize";
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
		return;
	}

	// -- Read parameters --
	const int  pitchAlgoIdx    = ParametersTonal::evalPitchalgo(inputs);
	const int  hpcpSize        = ParametersTonal::evalHpcpsize(inputs);
	const bool enablePitch     = ParametersTonal::evalEnablepitch(inputs);
	const bool enableHpcp      = ParametersTonal::evalEnablehpcp(inputs);
	const bool enableKey       = ParametersTonal::evalEnablekey(inputs);
	const bool enableDiss      = ParametersTonal::evalEnabledissonance(inputs);
	const bool enableInharm    = ParametersTonal::evalEnableinharmonicity(inputs);
	const bool musicalLabels   = ParametersTonal::evalMusicallabels(inputs);
	const bool enablePitchNote = ParametersTonal::evalEnablepitchnote(inputs);

	// -- Get upstream CHOP input --
	const OP_CHOPInput* chopIn = inputs->getInputCHOP(0);
	if (!chopIn || chopIn->numChannels < 1)
	{
		myError = "No input connected — expecting EssentiaCoreCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	// Infer sample rate from the upstream CHOP
	double sampleRate = chopIn->sampleRate;
	if (sampleRate <= 0.0) sampleRate = 44100.0;

	// -- Extract spectrum from "spectrum" channel --
	std::vector<float> specFloat;
	if (!extractChannelSamples(chopIn, "spectrum", specFloat) || specFloat.empty())
	{
		myWarning = "No spectrum channel found in input — connect EssentiaSpectrumCHOP";
		for (int ch = 0; ch < output->numChannels; ++ch)
			output->channels[ch][0] = 0.0f;
		return;
	}

	const int specBins = static_cast<int>(specFloat.size());

	// -- Reconfigure when topology changes --
	const bool configChanged =
		specBins   != mySpecBins         ||
		hpcpSize   != myHpcpSize         ||
		sampleRate != mySampleRate       ||
		pitchAlgoIdx        != myPitchAlgo           ||
		enablePitch         != myEnablePitch         ||
		enableHpcp          != myEnableHpcp          ||
		enableKey           != myEnableKey           ||
		enableDiss          != myEnableDissonance    ||
		enableInharm        != myEnableInharmonicity ||
		musicalLabels       != myMusicalLabels       ||
		enablePitchNote     != myEnablePitchNote;

	if (configChanged)
	{
		try
		{
			configureAlgorithms(specBins, hpcpSize, sampleRate, pitchAlgoIdx);

			mySpecBins             = specBins;
			myHpcpSize             = hpcpSize;
			mySampleRate           = sampleRate;
			myPitchAlgo            = pitchAlgoIdx;
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
	}

	// -- Copy float spectrum to essentia::Real buffer --
	mySpectrumBuf.resize(specBins);
	for (int i = 0; i < specBins; ++i)
		mySpectrumBuf[i] = static_cast<Real>(specFloat[i]);

	// ==========================================================
	// Pitch — PitchYinFFT
	// ==========================================================
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

	// ==========================================================
	// SpectralPeaks — shared intermediate for HPCP, Dissonance,
	// Inharmonicity, and Key (via HPCP->Key)
	// ==========================================================
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

	// ==========================================================
	// HPCP (chroma)
	// ==========================================================
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

	// ==========================================================
	// Key (requires HPCP vector as pcp input)
	// ==========================================================
	myKeyStr    = "";
	myScaleStr  = "";
	myKeyStrength = 0.0f;

	if (enableKey && myKey)
	{
		// Key works on a 12-bin PCP.  If HPCP size > 12 we still pass
		// the full vector — Essentia's Key algorithm accepts any size
		// that is a multiple of 12.
		// Use a stable buffer to avoid dangling reference from temporary
		if (myHpcpBuf.empty())
			myKeyPcpBuf.assign(12, 0.0f);
		else
			myKeyPcpBuf = myHpcpBuf;

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
			myKeyStr   = "";
			myScaleStr = "";
			myKeyStrength = 0.0f;
		}
	}

	// ==========================================================
	// Dissonance
	// ==========================================================
	myDissonanceVal = 0.0f;

	if (enableDiss && myDissonance)
	{
		if (myPeakFreqs.empty())
		{
			// No peaks — dissonance is undefined; output 0 silently
		}
		else
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
	}

	// ==========================================================
	// Inharmonicity
	// ==========================================================
	myInharmonicityVal = 0.0f;

	if (enableInharm && myInharmonicity)
	{
		if (myPeakFreqs.empty() || myPeakFreqs[0] <= 0.0f)
		{
			// No peaks or fundamental at 0 Hz — inharmonicity is undefined
		}
		else
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
	}

	// ==========================================================
	// Write output channels
	// ==========================================================
	// Channel layout matches rebuildChannelNames() order exactly:
	//   [pitch, pitch_confidence]  — if enablePitch
	//   [hpcp0 .. hpcpN]          — if enableHpcp
	//   [key, key_scale, key_strength] — if enableKey
	//   [dissonance]              — if enableDiss
	//   [inharmonicity]           — if enableInharm

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
				float midi = 12.0f * std::log2(myPitchHz / 440.0f) + 69.0f;
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
		if (myScaleStr == "major") scaleVal = 0.0f;
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

void EssentiaTonalCHOP::setupParameters(OP_ParameterManager* manager, void*)
{
	ParametersTonal::setup(manager);
}

// ---------------------------------------------------------------------------
// Info CHOP — expose diagnostic values as readable CHOP channels
// ---------------------------------------------------------------------------

int32_t EssentiaTonalCHOP::getNumInfoCHOPChans(void*)
{
	return 1; // spec_bins
}

void EssentiaTonalCHOP::getInfoCHOPChan(int32_t index,
                                          OP_InfoCHOPChan* chan, void*)
{
	if (index == 0)
	{
		chan->name->setString("spec_bins");
		chan->value = static_cast<float>(mySpecBins);
	}
}

// ---------------------------------------------------------------------------
// Diagnostic strings
// ---------------------------------------------------------------------------

void EssentiaTonalCHOP::getWarningString(OP_String* warning, void* /*reserved1*/)
{
	if (!myWarning.empty())
		warning->setString(myWarning.c_str());
}

void EssentiaTonalCHOP::getErrorString(OP_String* error, void* /*reserved1*/)
{
	if (!myError.empty())
		error->setString(myError.c_str());
}

// ===========================================================================
// Private helpers
// ===========================================================================

void EssentiaTonalCHOP::configureAlgorithms(int specBins, int hpcpSize,
                                              double sampleRate, int pitchAlgo)
{
	releaseAlgorithms();

	// Pre-allocate shared buffers
	mySpectrumBuf.resize(specBins, 0.0f);
	myPeakFreqs.reserve(100);
	myPeakMags.reserve(100);
	myHpcpBuf.resize(hpcpSize, 0.0f);

	const Real sr = static_cast<Real>(sampleRate);

	// Select pitch algorithm based on parameter index
	const char* pitchAlgoName = (pitchAlgo == 1) ? "PitchYinProbabilistic" : "PitchYinFFT";

	// Pitch detection — operates directly on the magnitude spectrum
	myPitchYinFFT = AlgorithmFactory::create(pitchAlgoName,
		"sampleRate", sr);

	// SpectralPeaks — sorted by frequency for downstream algorithms
	mySpectralPeaks = AlgorithmFactory::create("SpectralPeaks",
		"sampleRate", sr,
		"maxPeaks",   100,
		"orderBy",    std::string("frequency"));

	// HPCP — harmonic pitch class profile (chroma)
	myHpcp = AlgorithmFactory::create("HPCP",
		"size",       hpcpSize,
		"sampleRate", sr);

	// Key — key detection from pcp
	myKey = AlgorithmFactory::create("Key");

	// Dissonance
	myDissonance = AlgorithmFactory::create("Dissonance");

	// Inharmonicity
	myInharmonicity = AlgorithmFactory::create("Inharmonicity");
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
                                              bool hpcp, int hpcpSize,
                                              bool key, bool dissonance,
                                              bool inharmonicity,
                                              bool musicalLabels)
{
	static const char* noteNames12[] = {
		"c", "cs", "d", "ds", "e", "f", "fs", "g", "gs", "a", "as", "b"
	};

	myChannelNames.clear();

	if (pitch)
	{
		myChannelNames.emplace_back("pitch");
		myChannelNames.emplace_back("pitch_confidence");

		if (pitchNote)
			myChannelNames.emplace_back(musicalLabels ? "note" : "pitch_note");
	}

	if (hpcp)
	{
		if (!musicalLabels)
		{
			for (int i = 0; i < hpcpSize; ++i)
				myChannelNames.push_back("hpcp" + std::to_string(i));
		}
		else if (hpcpSize == 12)
		{
			for (int i = 0; i < 12; ++i)
				myChannelNames.emplace_back(noteNames12[i]);
		}
		else if (hpcpSize == 24)
		{
			// Two subdivisions per semitone: base, base+
			static const char* suffixes[] = { "", "+" };
			for (int semi = 0; semi < 12; ++semi)
				for (int sub = 0; sub < 2; ++sub)
					myChannelNames.push_back(
						std::string(noteNames12[semi]) + suffixes[sub]);
		}
		else if (hpcpSize == 36)
		{
			// Three subdivisions per semitone: base, base~, base+
			static const char* suffixes[] = { "", "~", "+" };
			for (int semi = 0; semi < 12; ++semi)
				for (int sub = 0; sub < 3; ++sub)
					myChannelNames.push_back(
						std::string(noteNames12[semi]) + suffixes[sub]);
		}
		else
		{
			// Fallback for unexpected sizes
			for (int i = 0; i < hpcpSize; ++i)
				myChannelNames.push_back("hpcp" + std::to_string(i));
		}
	}

	if (key)
	{
		myChannelNames.emplace_back("key");
		myChannelNames.emplace_back(musicalLabels ? "major_minor" : "key_scale");
		myChannelNames.emplace_back("key_strength");
	}

	if (dissonance)
		myChannelNames.emplace_back("dissonance");

	if (inharmonicity)
		myChannelNames.emplace_back("inharmonicity");
}

} // namespace EssentiaTD

// ===========================================================================
// DLL Entry Points
// ===========================================================================

using namespace EssentiaTD;

extern "C"
{

DLLEXPORT void FillCHOPPluginInfo(CHOP_PluginInfo* info)
{
	info->apiVersion = CHOPCPlusPlusAPIVersion;
	OP_CustomOPInfo& ci = info->customOPInfo;
	ci.opType->setString("Essentiatonal");
	ci.opLabel->setString("Essentia Tonal");
	ci.opIcon->setString("EST");
	ci.authorName->setString("Darien Brito");
	ci.authorEmail->setString("info@darienbrito.com");
	ci.minInputs = 1;
	ci.maxInputs = 1;
}

DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
{
	return new EssentiaTonalCHOP(info);
}

DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
{
	delete static_cast<EssentiaTonalCHOP*>(instance);
}

} // extern "C"
