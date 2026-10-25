#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Minimal TouchDesigner host stubs, shared by the headless CHOP tests. They
// exist so a test can drive the SHIPPED execute() path rather than a mirror of
// it: FakeInputs answers the handful of OP_Inputs methods the plugins call
// (getParDouble/Int/String, getInputCHOP, getTimeInfo, enablePar) and
// CaptureString collects whatever the CHOP reports through
// getWarningString/getErrorString.
//
// Unknown parameters deliberately return empty/zero so a test can leave a
// parameter unset and get the plugin's own documented fallback.

#include "CHOP_CPlusPlusBase.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace EssentiaTDTest
{

class FakeInputs : public TD::OP_Inputs
{
public:
	std::map<std::string, double>      dbl;
	std::map<std::string, int>         integer;
	std::map<std::string, std::string> str;
	const TD::OP_CHOPInput*            chop = nullptr;
	TD::OP_TimeInfo                    timeInfo{};

	FakeInputs()
	{
		std::memset(&timeInfo, 0, sizeof(timeInfo));
		timeInfo.rate     = 60.0;
		timeInfo.rootRate = 60.0;
	}

	// --- the six the plugins actually use ---------------------------------
	double getParDouble(const char* name, int32_t = 0) const override
	{
		auto it = dbl.find(name ? name : "");
		return it == dbl.end() ? 0.0 : it->second;
	}
	int32_t getParInt(const char* name, int32_t = 0) const override
	{
		auto it = integer.find(name ? name : "");
		return it == integer.end() ? 0 : it->second;
	}
	const char* getParString(const char* name) const override
	{
		auto it = str.find(name ? name : "");
		return it == str.end() ? "" : it->second.c_str();
	}
	const TD::OP_CHOPInput* getInputCHOP(int32_t index) const override
	{
		return index == 0 ? chop : nullptr;
	}
	const TD::OP_TimeInfo* getTimeInfo() const override { return &timeInfo; }
	void enablePar(const char*, bool) const override {}

	// --- remainder of the interface, never exercised ----------------------
	int32_t getNumInputs() const override { return chop ? 1 : 0; }
	const TD::OP_TOPInputOpenGL* getInputTOPOpenGL(int32_t) const override { return nullptr; }
	const TD::OP_DATInput* getParDAT(const char*) const override { return nullptr; }
	const TD::OP_TOPInputOpenGL* getParTOPOpenGL(const char*) const override { return nullptr; }
	const TD::OP_CHOPInput* getParCHOP(const char*) const override { return nullptr; }
	const TD::OP_ObjectInput* getParObject(const char*) const override { return nullptr; }
	bool getParDouble2(const char*, double& v0, double& v1) const override { v0 = v1 = 0; return false; }
	bool getParDouble3(const char*, double& v0, double& v1, double& v2) const override { v0 = v1 = v2 = 0; return false; }
	bool getParDouble4(const char*, double& v0, double& v1, double& v2, double& v3) const override { v0 = v1 = v2 = v3 = 0; return false; }
	bool getParInt2(const char*, int32_t& v0, int32_t& v1) const override { v0 = v1 = 0; return false; }
	bool getParInt3(const char*, int32_t& v0, int32_t& v1, int32_t& v2) const override { v0 = v1 = v2 = 0; return false; }
	bool getParInt4(const char*, int32_t& v0, int32_t& v1, int32_t& v2, int32_t& v3) const override { v0 = v1 = v2 = v3 = 0; return false; }
	const char* getParFilePath(const char*) const override { return ""; }
	bool getRelativeTransform(const char*, const char*, double[4][4]) const override { return false; }
	const TD::OP_DATInput* getDAT(const char*) const override { return nullptr; }
	const TD::OP_TOPInputOpenGL* getTOPOpenGL(const char*) const override { return nullptr; }
	const TD::OP_CHOPInput* getCHOP(const char*) const override { return nullptr; }
	const TD::OP_ObjectInput* getObject(const char*) const override { return nullptr; }
	void* getTOPDataInCPUMemory(const TD::OP_TOPInputOpenGL*,
	                            const TD::OP_TOPInputDownloadOptionsOpenGL*) const override { return nullptr; }
	const TD::OP_SOPInput* getParSOP(const char*) const override { return nullptr; }
	const TD::OP_SOPInput* getInputSOP(int32_t) const override { return nullptr; }
	const TD::OP_SOPInput* getSOP(const char*) const override { return nullptr; }
	const TD::OP_DATInput* getInputDAT(int32_t) const override { return nullptr; }
	PyObject* getParPython(const char*) const override { return nullptr; }
	const TD::OP_TOPInput* getTOP(const char*) const override { return nullptr; }
	const TD::OP_TOPInput* getInputTOP(int32_t) const override { return nullptr; }
	const TD::OP_TOPInput* getParTOP(const char*) const override { return nullptr; }
};

class CaptureString : public TD::OP_String
{
public:
	std::string value;
	void setString(const char* v) override { value = v ? v : ""; }
};

/// What a realtime drive observed. errorGaps is the lifecycle assertion:
/// execute() wipes myError every cook, so a config failure that is not
/// re-applied from the persistent slot shows for one frame and the op then
/// looks healthy while emitting zeros. Counting cooks alone cannot express
/// that, because the leading cooks spent accumulating a first FFT window
/// legitimately report nothing.
struct DriveResult
{
	int   cooks          = 0;   ///< cooks that produced output
	int   cooksWithError = 0;   ///< of those, how many reported an error
	int   firstErrorCook = -1;  ///< 1-based cook that first reported an error
	int   errorGaps      = 0;   ///< cooks after that one which reported none
	int   numChannels    = 0;   ///< channel count on the last cook
	std::vector<float> values;  ///< first sample of each channel, last cook
	std::string warning;        ///< last cook's warning string
	std::string error;          ///< last cook's error string
};

/// Drive a CHOP's shipped realtime path over `audio`, one timeslice per cook.
/// `cookBase` offsets the input's totalCooks stamp: RTFrameProcessor drops a
/// repeat stamp as a forced re-cook, so a second drive over the same CHOP must
/// continue the count rather than restart it. `trace`, when given, receives
/// every cook's first-sample channel values.
template <typename Chop>
DriveResult driveRealtime(Chop& chop, FakeInputs& in,
                          const std::vector<float>& audio,
                          int timeslice, double sampleRate,
                          int64_t cookBase = 0,
                          std::vector<std::vector<float>>* trace = nullptr)
{
	DriveResult r;
	const size_t cooks = timeslice > 0 ? audio.size() / timeslice : 0;

	for (size_t c = 0; c < cooks; ++c)
	{
		const float* slice   = audio.data() + c * static_cast<size_t>(timeslice);
		const float* chans[] = { slice };
		const char*  names[] = { "chan1" };

		TD::OP_CHOPInput input{};
		input.opPath      = "/audio";
		input.opId        = 1;
		input.numChannels = 1;
		input.numSamples  = timeslice;
		input.sampleRate  = sampleRate;
		input.startIndex  = 0;
		input.channelData = chans;
		input.nameData    = names;
		input.totalCooks  = cookBase + static_cast<int64_t>(c) + 1;
		in.chop           = &input;
		in.timeInfo.absFrame = cookBase + static_cast<int64_t>(c);

		TD::CHOP_OutputInfo info{};
		info.numChannels = 0;
		chop.getOutputInfo(&info, &in, nullptr);
		if (info.numChannels <= 0) continue;

		const int numSamp = info.numSamples > 0 ? info.numSamples : 1;
		std::vector<std::vector<float>> store(
			static_cast<size_t>(info.numChannels),
			std::vector<float>(static_cast<size_t>(numSamp), 0.0f));
		std::vector<float*> ptrs;
		for (auto& v : store) ptrs.push_back(v.data());
		std::vector<const char*> chName(static_cast<size_t>(info.numChannels), "ch");

		TD::CHOP_Output out(info.numChannels, numSamp, info.sampleRate,
		                    info.startIndex, ptrs.data(), chName.data());
		chop.execute(&out, &in, nullptr);

		CaptureString warn, err;
		chop.getWarningString(&warn, nullptr);
		chop.getErrorString(&err, nullptr);

		++r.cooks;
		if (!err.value.empty())
		{
			++r.cooksWithError;
			if (r.firstErrorCook < 0) r.firstErrorCook = r.cooks;
		}
		else if (r.firstErrorCook >= 0)
		{
			++r.errorGaps;
		}

		r.numChannels = info.numChannels;
		r.values.assign(static_cast<size_t>(info.numChannels), 0.0f);
		for (int ch = 0; ch < info.numChannels; ++ch)
			r.values[static_cast<size_t>(ch)] = store[static_cast<size_t>(ch)][0];
		r.warning = warn.value;
		r.error   = err.value;
		if (trace) trace->push_back(r.values);
	}
	return r;
}

} // namespace EssentiaTDTest
