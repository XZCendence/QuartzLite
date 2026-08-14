#pragma once

#include "QuartzTypes.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// near direct carryover struct from ue
enum class ELogLevel : uint8
{
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
	VeryVerbose,
};

// live lines get the freshness flash. preloaded ones must not light up a whole file on open
enum class ELogArrival : uint8
{
	Preloaded,
	Live,
};

struct FLogLine
{
	std::string Text;        // message without timestamp/frame/category/level
	std::string Category;    // empty when the line had no prefix
	std::string Timestamp;   // "12:34:56.789", empty when none
	ELogLevel   Level = ELogLevel::Log;
	int32       Frame = -1;      // the [  N] bracket, -1 when absent
	double      AddedAt = 0.0;   // imgui clock, for the flash and age
	int32       Hour = -1;       // -1 = unknown
	int32       Minute = 0;
	int32       Second = 0;
	int32       Millisecond = 0;
};

class FLogSource final
{
public:
	static constexpr int32 MaxLines = 50000;

	std::string Name;         // window title
	std::string Path;         // absolute path being tailed
	bool        bOpen = true; // closing the window also mutes the tailer

	void Add(std::vector<FLogLine>&& Batch, ELogArrival Arrival)
	{
		std::lock_guard<std::mutex> Lock(PendingMutex);
		for (FLogLine& Line : Batch)
		{
			// live lines get a stamp during Drain. preloaded ones stay 0
			Line.AddedAt = Arrival == ELogArrival::Live ? -1.0 : 0.0;
			Pending.push_back(std::move(Line));
		}
	}

	void SetStatus(const std::string& InStatus)
	{
		std::lock_guard<std::mutex> Lock(PendingMutex);
		Status = InStatus;
	}
	std::string GetStatus()
	{
		std::lock_guard<std::mutex> Lock(PendingMutex);
		return Status;
	}

	// ui thread only. stamps arrival times on the ui clock
	void Drain(double Now)
	{
		std::vector<FLogLine> Batch;
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			if (Pending.empty())
			{
				return;
			}
			Batch.swap(Pending);
		}
		for (FLogLine& Line : Batch)
		{
			if (Line.AddedAt < 0.0)
			{
				Line.AddedAt = Now;
			}
			Items.push_back(std::move(Line));
		}
		if ((int32)Items.size() > MaxLines)
		{
			const size_t Excess = Items.size() - MaxLines;
			Items.erase(Items.begin(), Items.begin() + Excess);
			Dropped += (int64)Excess;
		}
	}

	const std::vector<FLogLine>& GetItems() const { return Items; }

	// counted as evictions so a selection holding old ids prunes itself
	void Clear()
	{
		Dropped += (int64)Items.size();
		Items.clear();
	}

	// row N of GetItems is the (GetDroppedCount + N)'th line ever seen
	int64 GetDroppedCount() const { return Dropped; }

	std::atomic<bool> bCollecting{ true };
	std::atomic<bool> bStop{ false };   // tail-thread shutdown

private:
	std::mutex            PendingMutex;
	std::vector<FLogLine> Pending;      // cross-thread inbox
	std::string           Status;
	std::vector<FLogLine> Items;
	int64                 Dropped = 0;
};
