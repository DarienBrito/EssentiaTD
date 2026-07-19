#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Spectrum.h"
#include "Shared/RTFrameProcessor.h"

#include <string>

namespace EssentiaTD
{

class EssentiaSpectrumCHOP : public TD::CHOP_CPlusPlusBase
{
public:
	EssentiaSpectrumCHOP(const TD::OP_NodeInfo* info);
	~EssentiaSpectrumCHOP() override;

	void getGeneralInfo(TD::CHOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*) override;
	bool getOutputInfo(TD::CHOP_OutputInfo* info, const TD::OP_Inputs* inputs, void*) override;
	void getChannelName(int32_t index, TD::OP_String* name,
		const TD::OP_Inputs*, void*) override;

	void execute(TD::CHOP_Output* output, const TD::OP_Inputs* inputs, void*) override;

	void setupParameters(TD::OP_ParameterManager* manager, void*) override;

	int32_t getNumInfoCHOPChans(void*) override;
	void getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* chan, void*) override;

	void getWarningString(TD::OP_String* warning, void* reserved1) override;
	void getErrorString(TD::OP_String* error, void* reserved1) override;

private:
	// Ring-buffer accumulation + Windowing/FFT/CartesianToPolar
	RTFrameProcessor myFrameProc;

	// Config tracking (myFrameProc reconfigures only when these change)
	int myFftSize = 0;
	int myHopSize = 0;
	int myZeroPadding = 0;
	double mySampleRate = 0.0;
	std::string myWindowType;

	// Init state
	bool myInitOk = false;

	// Error / warning
	std::string myError;
	std::string myWarning;
};

} // namespace EssentiaTD
