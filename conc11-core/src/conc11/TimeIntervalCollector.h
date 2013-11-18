#pragma once

#include <framework/Bitfields.h>
#include <framework/HighResClock.h>

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

namespace conc11
{
	
struct TimeInterval
{
	HighResTimePointType start;
	HighResTimePointType end;
	Color color;
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

class ScopedTimeInterval
{
public:
	
	ScopedTimeInterval(TimeInterval& interval, Color color, const std::shared_ptr<TimeIntervalCollector>& collector)
	: m_interval(interval)
	, m_collector(collector)
	{
		m_interval.color = color;
		m_interval.start = HighResClock::now();
	}

	~ScopedTimeInterval()
	{
		m_interval.end = HighResClock::now();
		if (std::shared_ptr<TimeIntervalCollector> collector = m_collector.lock())
			collector->insert(m_interval);
	}

private:

	ScopedTimeInterval(const ScopedTimeInterval&);
	ScopedTimeInterval& operator=(const ScopedTimeInterval&);

	TimeInterval& m_interval;
	std::weak_ptr<TimeIntervalCollector> m_collector;
};

} // namespace conc11
