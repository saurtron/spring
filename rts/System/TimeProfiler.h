/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef TIME_PROFILER_H
#define TIME_PROFILER_H

#include <atomic>
#include <string>
#include <deque>
#include <vector>
#include <array>

#include "System/Misc/SpringTime.h"
#include "System/Misc/NonCopyable.h"
#include "System/float3.h"
#include "System/StringHash.h"
#include "System/UnorderedMap.hpp"

#include "System/Misc/TracyDefs.h"

// disable these for minimal profiling; all special
// timers contribute even when profiler is disabled
// NB: names are assumed to be compile-time literals
#define SCOPED_TIMER(      name)  ZoneScopedNC(name, tracy::Color::Goldenrod); static TimerNameRegistrar __tnr(name); ScopedTimer __scopedTimer(hashString(name));
#define SCOPED_TIMER_NOREG(name)  ZoneScopedNC(name, tracy::Color::Goldenrod);                                        ScopedTimer __scopedTimer(hashString(name));

#define SCOPED_SPECIAL_TIMER(      name)  static TimerNameRegistrar __stnr(name); ScopedTimer __scopedTimer(hashString(name), false, true);
#define SCOPED_SPECIAL_TIMER_NOREG(name)                                          ScopedTimer __scopedTimer(hashString(name), false, true);

#define SCOPED_MT_TIMER(name)  ScopedMtTimer __scopedTimer(hashString(name));

#define SCOPED_ONCE_TIMER(name) ZoneScopedNC(name, tracy::Color::Purple); ScopedOnceTimer __timer(name);

class BasicTimer : public spring::noncopyable
{
public:
	//BasicTimer(const spring_time time): nameHash(0), startTime(time) {}
	BasicTimer(unsigned _nameHash) : nameHash(_nameHash), startTime(spring_gettime()) { }

	spring_time GetDuration() const;

protected:
	const unsigned nameHash;
	const spring_time startTime;
};


/**
 * @brief Time profiling helper class
 *
 * Construct an instance of this class where you want to begin time measuring,
 * and destruct it at the end (or let it be autodestructed).
 */
class ScopedTimer : public BasicTimer
{
public:
	ScopedTimer(const unsigned _nameHash, bool _autoShowGraph = false, bool _specialTimer = false);
	~ScopedTimer();

private:
	const bool autoShowGraph;
	const bool specialTimer;
};


class ScopedMtTimer : public BasicTimer
{
public:
	ScopedMtTimer(const unsigned _nameHash, bool _autoShowGraph = false);
	~ScopedMtTimer();

private:
	const bool autoShowGraph;
};



/**
 * @brief print passed time to infolog
 */
class ScopedOnceTimer
{
public:
	ScopedOnceTimer(const std::string& name, const char* frmt = "[%s][%s] %ims");
	ScopedOnceTimer(const char* name, const char* frmt = "[%s][%s] %ims");
	~ScopedOnceTimer();

	spring_time GetDuration() const;

protected:
	const spring_time startTime;

	char name[128];
	char frmt[128];
};



class CTimeProfiler
{
public:
	CTimeProfiler();
	~CTimeProfiler();

	static CTimeProfiler& GetInstance();

	static bool RegisterTimer(const char* name);
	static bool UnRegisterTimer(const char* name);

	struct TimeRecord {
		TimeRecord() {
			frames.fill(spring_time(0));
		}

		static constexpr unsigned numFrames = 128;

		spring_time total = spring_notime;
		spring_time current = spring_notime;
		std::array<spring_time, numFrames> frames;

		// .x := maximum dt, .y := time-percentage, .z := peak-percentage
		float3 stats;
		float3 color;

		bool newPeak = false;
		bool newLagPeak = false;
		bool showGraph = false;
	};

	enum SortType {
		ST_ALPHABETICAL = 0,
		ST_TOTALTIME    = 1,
		ST_CURRENTTIME  = 2,
		ST_MAXTIME      = 3,
		ST_LAG          = 4,
		ST_COUNT        = 5
	};

	using TimeRecordPair = std::pair<std::string, TimeRecord>;
	using ProfileSortFunc = bool(*)(const TimeRecordPair&, const TimeRecordPair&);

	static const std::array<ProfileSortFunc, SortType::ST_COUNT> SortingFunctions;
public:
	std::vector< std::pair<std::string, TimeRecord> >& GetSortedProfiles() { return sortedProfiles; }
	std::vector< std::deque< std::pair<spring_time, spring_time> > >& GetThreadProfiles() { return threadProfiles; }

	size_t GetNumSortedProfiles() const { return (sortedProfiles.size()); }
	size_t GetNumThreadProfiles() const { return (threadProfiles.size()); }

	float GetTimePercentage(const char* name) const { return (GetTimeRecord(name).stats.y); }
	float GetTimePercentageRaw(const char* name) const { return (GetTimeRecordRaw(name).stats.y); }

	const TimeRecord& GetTimeRecord(const char* name) const;
	const TimeRecord& GetTimeRecordRaw(const char* name) const {
		// do not default-create keys, breaks resorting
		const auto it = profiles.find(hashString(name));
		const static TimeRecord tr;

		if (it == profiles.end())
			return tr;

		return (it->second);
	}

	void ToggleLock(bool lock);
	void ResetState();
	void ResetPeaks() {
		ToggleLock(true);

		for (auto& p: profiles)
			p.second.stats.z = 0.0f;

		ToggleLock(false);
	}

	void SetSortingType(SortType st) {
		sortingType = st;
		++resortProfiles;
	}

	void Update();
	void UpdateRaw();

	void ResortProfilesRaw();
	void RefreshProfiles();
	void RefreshProfilesRaw();

	void SetEnabled(bool b) { enabled = b; }
	void PrintProfilingInfo() const;

	void AddTime(
		unsigned nameHash,
		const spring_time startTime,
		const spring_time deltaTime,
		const bool showGraph = false,
		const bool specialTimer = false,
		const bool threadTimer = false
	);
	void AddTimeRaw(
		unsigned nameHash,
		const spring_time startTime,
		const spring_time deltaTime,
		const bool showGraph,
		const bool threadTimer
	);

private:
	SortType sortingType = SortType::ST_ALPHABETICAL;
	spring::unordered_map<unsigned, TimeRecord> profiles;

	std::vector< std::pair<std::string, TimeRecord> > sortedProfiles;
	std::vector< std::deque< std::pair<spring_time, spring_time> > > threadProfiles;

	spring_time lastBigUpdate;

	/// increases each update, from 0 to (numFrames-1)
	unsigned currentPosition;
	unsigned resortProfiles;

	// if false, AddTime is a no-op for (almost) all timers
	std::atomic<bool> enabled;
};


class TimerNameRegistrar : public spring::noncopyable
{
public:
	TimerNameRegistrar(const char* timerName) {
		CTimeProfiler::RegisterTimer(timerName);
	}
};

#endif // TIME_PROFILER_H
