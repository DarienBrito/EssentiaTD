#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <essentia/algorithmfactory.h>
#include <essentia/debugging.h>
#include <mutex>
#include <string>

namespace EssentiaTD
{

/// Call once before using any Essentia algorithms.
/// Thread-safe via std::call_once — safe to call from multiple DLL entry points.
/// Returns true on success, false if init threw an exception.
inline bool ensureEssentiaInit(std::string& errorOut)
{
	static std::once_flag sFlag;
	static bool sOk = false;
	static std::string sError;

	std::call_once(sFlag, [&]() {
		try {
			essentia::init();
			// Essentia's Logger is one unguarded std::deque shared by every
			// thread in this plugin: a batch worker and a realtime cook both
			// inside compute() race it (PitchYinFFT warns per frame). Output
			// only ever reached stderr, which TD does not surface.
			essentia::warningLevelActive = false;
			essentia::infoLevelActive    = false;
			sOk = true;
		}
		catch (const std::exception& e) {
			sError = std::string("essentia::init() failed: ") + e.what();
		}
		catch (...) {
			sError = "essentia::init() failed with unknown exception";
		}
	});

	if (!sOk)
		errorOut = sError;
	return sOk;
}

} // namespace EssentiaTD
