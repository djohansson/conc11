#pragma once

#include <framework/Bitfields.h>

#include <cassert>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

namespace conc11
{
	
typedef std::chrono::high_resolution_clock ClockType;
typedef std::chrono::time_point<ClockType> TimePointType;
	
struct TimeInterval
{
	TimePointType start;
	TimePointType end;
	Bitfields<8, 8, 8, 8> color;
	std::string name;
};

class TimeIntervalCollector
{
public:

	typedef std::multimap<std::thread::id, TimeInterval> ContainerType;

	inline void insert(const TimeInterval& ti)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_intervals.insert(std::make_pair(std::this_thread::get_id(), ti));
	}

	inline void clear()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_intervals.clear();
	}

	inline const ContainerType& getIntervals() const
	{
		return m_intervals;
	}

private:

	ContainerType m_intervals;
	std::mutex m_mutex;
};
	
extern TimeIntervalCollector* g_timeIntervalCollector;

class ScopedTimeInterval
{
public:
	
	ScopedTimeInterval(TimeInterval& interval, TimeIntervalCollector& collector)
	: m_interval(interval)
	, m_collector(collector)
	{
		m_interval.start = s_clock.now();
	}

	~ScopedTimeInterval()
	{
		m_interval.end = s_clock.now();
		m_collector.insert(m_interval);
	}

private:

	ScopedTimeInterval(const ScopedTimeInterval&);
	ScopedTimeInterval& operator=(const ScopedTimeInterval&);

	TimeInterval& m_interval;
	TimeIntervalCollector& m_collector;
	static ClockType s_clock;
};

} // namespace conc11
