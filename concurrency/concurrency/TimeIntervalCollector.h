#pragma once

#include "Types.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace conc11
{

class TimeIntervalCollector
{
public:

	typedef std::chrono::high_resolution_clock ClockType;

	struct TimeInterval
	{
		typedef std::chrono::time_point<ClockType> TimePointType;

		TimeInterval(TimePointType&& start_)
			: start(std::forward<TimePointType>(start_))
		{ }

		std::chrono::time_point<ClockType> start;
		std::chrono::time_point<ClockType> end;
	};

	typedef ConcurrentUnorderedMultiMapType<std::thread::id, TimeInterval> ContainerType;
	typedef ContainerType::iterator Handle;

    TimeIntervalCollector()
    { }
    
    ~TimeIntervalCollector()
    { }

	inline Handle begin()
	{
		return m_intervals.insert(std::make_pair(std::this_thread::get_id(), TimeInterval(m_clock.now())));
	}

	inline void end(Handle handle)
	{
		(*handle).second.end = m_clock.now();
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
    
    ScopedTimeInterval(std::shared_ptr<TimeIntervalCollector> collector)
        : m_collector(collector)
    {
		if (m_collector)
			m_handle = m_collector->begin();
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
	TimeIntervalCollector::Handle m_handle;
};

}

