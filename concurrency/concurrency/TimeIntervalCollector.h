#pragma once

#include "Types.h"

#include <cassert>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <type_traits>

namespace conc11
{

class TimeIntervalCollector
{
public:

	typedef std::chrono::high_resolution_clock ClockType;

	struct TimeInterval
	{
		typedef std::chrono::time_point<ClockType> TimePointType;

		TimeInterval()
		{ }

		TimeInterval(const TimePointType& start_)
			: start(start_)
		{ }

		TimeInterval(TimePointType&& start_)
			: start(std::forward<TimePointType>(start_))
		{ }

		std::chrono::time_point<ClockType> start;
		std::chrono::time_point<ClockType> end;
		std::string debugName;
		float debugColor[3];
	};

	typedef ConcurrentUnorderedMultiMapType<std::thread::id, TimeInterval, std::hash<std::thread::id>> ContainerType;
	typedef std::pair<std::thread::id, TimeInterval> HandleType;

	TimeIntervalCollector()
	{ }

	~TimeIntervalCollector()
	{ }

	inline HandleType begin()
	{
		return std::make_pair(std::this_thread::get_id(), TimeInterval(m_clock.now()));
	}

	inline void end(HandleType handle)
	{
		handle.second.end = m_clock.now();

		m_intervals.insert(handle);
	}

	inline void clear()
	{
		m_intervals.clear();
	}

	inline const ContainerType& getIntervals() const
	{
		return m_intervals;
	}

private:

	ClockType m_clock;
	ContainerType m_intervals;
};

class ScopedTimeInterval
{
public:

	ScopedTimeInterval(std::shared_ptr<TimeIntervalCollector> collector, const std::string& debugName = "", const float* debugColor = nullptr)
		: m_collector(collector)
	{
		if (m_collector)
		{
			m_handle = m_collector->begin();
			
			TimeIntervalCollector::TimeInterval& ti = m_handle.second;
			ti.debugName = debugName;
			if (debugColor != nullptr)
			{
				ti.debugColor[0] = debugColor[0];
				ti.debugColor[1] = debugColor[1];
				ti.debugColor[2] = debugColor[2];
			}
		}
	}

	~ScopedTimeInterval()
	{
		if (m_collector)
			m_collector->end(m_handle);
	}

private:

	ScopedTimeInterval(const ScopedTimeInterval&);
	ScopedTimeInterval& operator=(const ScopedTimeInterval&);

	std::shared_ptr<TimeIntervalCollector> m_collector;
	TimeIntervalCollector::HandleType m_handle;
};

}

