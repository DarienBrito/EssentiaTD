#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CRTP base class for unified real-time + batch analysis CHOPs.
// Consolidates mode switching, async polling, init, input validation,
// trigger logic, output writing, and Info CHOP plumbing.
//
// Derived classes implement:
//   getOutputInfoImpl(info, inputs, isBatch)
//   getChannelNameImpl(index, name, inputs)
//   executeRealtimeImpl(output, inputs)     — full real-time processing
//   snapshotAndLaunch(audio, inputs)        — launch async batch worker
//   setupParametersImpl(manager)            — all params (Mode + shared + mode-specific)
//   getNumInfoCHOPChansImpl()
//   getInfoCHOPChanImpl(index, chan)
//
// Optional overrides:
//   onResultCollected(result)  — extract CHOP-specific fields
//   writeOutputBatch(output, inputs)  — custom batch output (e.g. normalization)

#include "CHOP_CPlusPlusBase.h"
#include "AsyncBatchRunner.h"
#include "BatchCommon.h"
#include "EssentiaInit.h"

#include <algorithm>
#include <string>
#include <vector>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// DLL entry point macro — place at end of each unified CHOP .cpp
// ---------------------------------------------------------------------------

#define UNIFIED_CHOP_DLL_EXPORT(ClassName, OpType, OpLabel, OpIcon)             \
using namespace EssentiaTD;                                                     \
extern "C"                                                                      \
{                                                                               \
DLLEXPORT void FillCHOPPluginInfo(TD::CHOP_PluginInfo* info)                    \
{                                                                               \
    info->apiVersion = TD::CHOPCPlusPlusAPIVersion;                             \
    auto& ci = info->customOPInfo;                                              \
    ci.opType->setString(OpType);                                               \
    ci.opLabel->setString(OpLabel);                                             \
    ci.opIcon->setString(OpIcon);                                               \
    ci.authorName->setString("Darien Brito");                                   \
    ci.authorEmail->setString("info@darienbrito.com");                          \
    ci.minInputs = 1;                                                           \
    ci.maxInputs = 1;                                                           \
}                                                                               \
DLLEXPORT TD::CHOP_CPlusPlusBase* CreateCHOPInstance(const TD::OP_NodeInfo* i)  \
{                                                                               \
    return new ClassName(i);                                                    \
}                                                                               \
DLLEXPORT void DestroyCHOPInstance(TD::CHOP_CPlusPlusBase* instance)            \
{                                                                               \
    delete static_cast<ClassName*>(instance);                                   \
}                                                                               \
}

// ---------------------------------------------------------------------------
// UnifiedCHOPBase — CRTP template
// ---------------------------------------------------------------------------

template <typename Derived>
class UnifiedCHOPBase : public TD::CHOP_CPlusPlusBase
{
public:
	explicit UnifiedCHOPBase(const TD::OP_NodeInfo*)
	{
		std::string initErr;
		myInitOk = ensureEssentiaInit(initErr);
		if (!myInitOk) myError = initErr;
	}

	~UnifiedCHOPBase() override
	{
		myRunner.cancelAndWait();
	}

	// ----- Mode helper -----

	bool isBatchMode(const TD::OP_Inputs* inputs) const
	{
		return evalMode(inputs) == 1;
	}

	// ----- TD overrides -----

	void getGeneralInfo(TD::CHOP_GeneralInfo* ginfo,
	                    const TD::OP_Inputs* inputs, void*) override
	{
		if (isBatchMode(inputs))
		{
			ginfo->cookEveryFrame        = false;
			ginfo->cookEveryFrameIfAsked = myRunner.isComputing()
			                            || (evalBatchAutocompute(inputs) && !myHasResults);
			ginfo->timeslice             = false;
		}
		else
		{
			ginfo->cookEveryFrame        = false;
			ginfo->cookEveryFrameIfAsked = true;
			// timeslice must stay false: TD's timeslice engine intermittently
			// raises a sticky "Sample rate is zero" error on a timesliced
			// custom CHOP's first cook (see docs/lessons-chop-output.md §5)
			ginfo->timeslice             = false;
		}
		ginfo->inputMatchIndex = -1;
	}

	bool getOutputInfo(TD::CHOP_OutputInfo* info,
	                   const TD::OP_Inputs* inputs, void*) override
	{
		const bool batch = isBatchMode(inputs);

		// Show/hide batch-only params
		inputs->enablePar(BatchComputeName, batch);
		inputs->enablePar(BatchAutocomputeName, batch);

		return self()->getOutputInfoImpl(info, inputs, batch);
	}

	void getChannelName(int32_t index, TD::OP_String* name,
	                    const TD::OP_Inputs* inputs, void*) override
	{
		self()->getChannelNameImpl(index, name, inputs);
	}

	void execute(TD::CHOP_Output* output,
	             const TD::OP_Inputs* inputs, void*) override
	{
		myError.clear();
		myWarning.clear();

		if (isBatchMode(inputs))
			executeBatch(output, inputs);
		else
			self()->executeRealtimeImpl(output, inputs);
	}

	void setupParameters(TD::OP_ParameterManager* manager, void*) override
	{
		self()->setupParametersImpl(manager);
	}

	int32_t getNumInfoCHOPChans(void*) override
	{
		return self()->getNumInfoCHOPChansImpl();
	}

	void getInfoCHOPChan(int32_t index,
	                     TD::OP_InfoCHOPChan* chan, void*) override
	{
		self()->getInfoCHOPChanImpl(index, chan);
	}

	void getWarningString(TD::OP_String* warning, void*) override
	{
		if (!myWarning.empty()) warning->setString(myWarning.c_str());
	}

	void getErrorString(TD::OP_String* error, void*) override
	{
		if (!myError.empty()) error->setString(myError.c_str());
	}

protected:
	// ----- Default hook implementations (override in Derived as needed) -----

	/// Called after a successful batch result is collected.
	void onResultCollected(AsyncBatchResult&) {}

	/// Write cached batch results to output. Override for custom post-processing.
	void writeOutputBatch(TD::CHOP_Output* output, const TD::OP_Inputs*)
	{
		if (myHasResults)
		{
			const int numCh = std::min(output->numChannels,
			                           static_cast<int>(myResultCache.size()));
			const int numSamp = output->numSamples;

			for (int c = 0; c < numCh; ++c)
			{
				const auto& chData = myResultCache[c];
				for (int s = 0; s < numSamp; ++s)
					output->channels[c][s] = (s < static_cast<int>(chData.size()))
					                         ? chData[s] : 0.0f;
			}
			for (int c = numCh; c < output->numChannels; ++c)
				for (int s = 0; s < output->numSamples; ++s)
					output->channels[c][s] = 0.0f;
		}
		else
		{
			zeroOutput(output);
		}
	}

	// ----- Shared state -----

	AsyncBatchRunner                myRunner;
	std::vector<std::vector<float>> myResultCache;
	int    myCachedNumFrames  = 0;
	float  myCachedSampleRate = 0.0f;
	bool   myHasResults       = false;
	AudioFingerprint myLastFingerprint;
	bool        myInitOk = false;
	std::string myError;
	std::string myWarning;

	static void zeroOutput(TD::CHOP_Output* output)
	{
		for (int c = 0; c < output->numChannels; ++c)
			for (int s = 0; s < output->numSamples; ++s)
				output->channels[c][s] = 0.0f;
	}

private:
	Derived*       self()       { return static_cast<Derived*>(this); }
	const Derived* self() const { return static_cast<const Derived*>(this); }

	/// Batch execution skeleton — called when Mode == batch
	void executeBatch(TD::CHOP_Output* output, const TD::OP_Inputs* inputs)
	{
		// 1. Poll async result
		{
			AsyncBatchResult result;
			if (myRunner.tryCollectResult(result))
			{
				if (result.success)
				{
					myResultCache      = std::move(result.cache);
					myCachedNumFrames  = result.numFrames;
					myCachedSampleRate = result.sampleRate;
					myHasResults       = true;
					if (!result.warning.empty())
						myWarning = result.warning;
					self()->onResultCollected(result);
				}
				else
				{
					if (!result.error.empty())
						myError = result.error;
					if (!result.warning.empty())
						myWarning = result.warning;
				}
			}
		}

		// 2. Show computing progress
		if (myRunner.isComputing())
		{
			myWarning = "Computing... " +
				std::to_string(static_cast<int>(myRunner.progress() * 100.0f)) + "%";
		}

		// 3. Check init
		if (!myInitOk)
		{
			myError = "Essentia failed to initialize";
			zeroOutput(output);
			return;
		}

		// 4. Check input
		const TD::OP_CHOPInput* audioIn = inputs->getInputCHOP(0);
		if (!audioIn || audioIn->numChannels < 1 || audioIn->numSamples < 1)
		{
			myError = "No audio input connected";
			zeroOutput(output);
			return;
		}

		// 5. Trigger computation
		if (!myRunner.isComputing())
		{
			const bool pulse       = evalBatchCompute(inputs);
			const bool autoCompute = evalBatchAutocompute(inputs);
			AudioFingerprint fp    = makeFingerprint(audioIn);
			const bool changed     = (fp != myLastFingerprint);

			if (pulse || (autoCompute && (changed || !myHasResults)))
			{
				myLastFingerprint = fp;

				AudioSnapshot audio;
				audio.numSamples = audioIn->numSamples;
				audio.sampleRate = (audioIn->sampleRate > 0.0)
				                   ? audioIn->sampleRate : 44100.0;
				audio.data.resize(audio.numSamples);
				const float* src = audioIn->getChannelData(0);
				std::copy(src, src + audio.numSamples, audio.data.begin());

				self()->snapshotAndLaunch(std::move(audio), inputs);
			}
		}

		// 6. Write output
		self()->writeOutputBatch(output, inputs);
	}
};

} // namespace EssentiaTD
