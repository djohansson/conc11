#pragma once

#include <list>

struct TimeInterval
{
};

class TimeIntervalCollector
{
public:
    
    typedef TimeInterval TimeIntervalType;
    typedef std::list<TimeIntervalType> ContainerType;
    
    TimeIntervalCollector()
    { }
    
    ~TimeIntervalCollector()
    { }
    
private:
    
    std::list<TimeInterval> m_intervals;
    
};

class ScopedTimeInterval
{
public:
    
    ScopedTimeInterval(TimeIntervalCollector* collector)
        : m_collector(collector)
    { }
    
    ~ScopedTimeInterval()
    { }
    
private:
    
    ScopedTimeInterval(const ScopedTimeInterval&);
	ScopedTimeInterval& operator=(const ScopedTimeInterval&);
    
    TimeIntervalCollector* m_collector;
};
