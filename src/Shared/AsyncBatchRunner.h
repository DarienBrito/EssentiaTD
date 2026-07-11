#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reusable async computation runner for batch analysis CHOPs.
// Offloads heavy processing to a background thread so TD's main cook
// thread never blocks.  Header-only.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace EssentiaTD
{

// ---------------------------------------------------------------------------
// Deep copy of audio input (OP_CHOPInput* is transient)
// ---------------------------------------------------------------------------

struct AudioSnapshot
{
	std::vector<float> data;
	int    numSamples = 0;
	double sampleRate = 0.0;
};

// ---------------------------------------------------------------------------
// Result returned by the async worker
// ---------------------------------------------------------------------------

struct AsyncBatchResult
{
	std::vector<std::vector<float>> cache;          // [channel][frame]
	std::vector<std::string>        channelNames;   // optional
	int    numFrames   = 0;
	float  sampleRate  = 0.0f;
	bool   success     = false;
	std::string error;
	std::string warning;

	// CHOP-specific extras
	float detectedBpm   = 0.0f;
	bool  usedAutocorr  = false;
};

// ---------------------------------------------------------------------------
// AsyncBatchRunner
// ---------------------------------------------------------------------------

class AsyncBatchRunner
{
public:
	enum class Status { Idle, Computing, Done };

	using WorkFn = std::function<AsyncBatchResult(
		const std::atomic<bool>& cancelFlag,
		std::atomic<float>&      progress)>;

	~AsyncBatchRunner() { cancelAndWait(); }

	// Launch a new computation. Cancels any in-flight work first.
	void launch(WorkFn workFn)
	{
		cancelAndWait();

		myCancelRequested.store(false, std::memory_order_relaxed);
		myProgress.store(0.0f, std::memory_order_relaxed);
		myStatus.store(Status::Computing, std::memory_order_release);

		myThread = std::thread([this, fn = std::move(workFn)]()
		{
			AsyncBatchResult result;
			try {
				result = fn(myCancelRequested, myProgress);
			}
			catch (const std::exception& e) {
				result.success = false;
				result.error = std::string("Async compute failed: ") + e.what();
			}
			catch (...) {
				result.success = false;
				result.error = "Async compute failed with unknown error";
			}

			{
				std::lock_guard<std::mutex> lock(myResultMutex);
				myStagedResult = std::move(result);
			}

			if (myCancelRequested.load(std::memory_order_relaxed))
				myStatus.store(Status::Idle, std::memory_order_release);
			else
				myStatus.store(Status::Done, std::memory_order_release);
		});
	}

	// Poll status (lock-free).
	Status status()      const { return myStatus.load(std::memory_order_acquire); }
	bool   isComputing() const { return status() == Status::Computing; }

	// If Done, moves result into outResult and resets to Idle. Returns true if swapped.
	bool tryCollectResult(AsyncBatchResult& outResult)
	{
		if (myStatus.load(std::memory_order_acquire) != Status::Done)
			return false;

		{
			std::lock_guard<std::mutex> lock(myResultMutex);
			outResult = std::move(myStagedResult);
		}

		if (myThread.joinable())
			myThread.join();

		myStatus.store(Status::Idle, std::memory_order_release);
		return true;
	}

	// Cancel in-flight work and block until thread exits.
	void cancelAndWait()
	{
		myCancelRequested.store(true, std::memory_order_release);
		if (myThread.joinable())
			myThread.join();
		// If the worker finished with a real result before it observed the
		// cancel request, leave Status::Done so a later tryCollectResult() can
		// still retrieve it (launch() overwrites this to Computing anyway).
		// Only force Idle when no collectable result is staged.
		if (myStatus.load(std::memory_order_acquire) != Status::Done)
			myStatus.store(Status::Idle, std::memory_order_release);
	}

	// Progress (0.0 to 1.0), set by the worker.
	float progress() const { return myProgress.load(std::memory_order_relaxed); }

private:
	std::thread          myThread;
	std::atomic<Status>  myStatus{ Status::Idle };
	std::atomic<bool>    myCancelRequested{ false };
	std::atomic<float>   myProgress{ 0.0f };
	std::mutex           myResultMutex;
	AsyncBatchResult     myStagedResult;
};

} // namespace EssentiaTD
