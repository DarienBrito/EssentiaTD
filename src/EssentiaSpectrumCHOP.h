#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CHOP_CPlusPlusBase.h"
#include "Parameters_Spectrum.h"

#include <essentia/algorithmfactory.h>
#include <vector>
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
	void configureAlgorithms(int fftSize, const char* windowType);
	void releaseAlgorithms();
	void processFrame();

	// State
	int myFftSize = 0;
	int myHopSize = 0;
	double mySampleRate = 0.0;
	std::string myWindowType;

	// Essentia algorithms (owned, deleted in releaseAlgorithms)
	essentia::standard::Algorithm* myWindowing = nullptr;
	essentia::standard::Algorithm* mySpectrum = nullptr;

	// Pre-allocated buffers
	std::vector<essentia::Real> myAudioFrame;
	std::vector<essentia::Real> myWindowedFrame;
	std::vector<essentia::Real> mySpectrumMag;

	// Init state
	bool myInitOk = false;

	// Error / warning
	std::string myError;
	std::string myWarning;
};

} // namespace EssentiaTD
