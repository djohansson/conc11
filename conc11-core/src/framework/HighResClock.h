#pragma once

#include <Module.h>

#include <chrono>

#if defined (_MSC_VER)
namespace highresclock
{
CONC11_CORE_API long long getFrequency();
CONC11_CORE_API long long getCounter();
}
struct HighResClock
{
	typedef long long rep;
	typedef std::nano period;
	typedef std::chrono::duration<rep, period> duration;
	typedef std::chrono::time_point<HighResClock> time_point;
	static const bool is_steady = true;

	static inline time_point now()
	{
		return time_point(duration(highresclock::getCounter() * static_cast<rep>(period::den) / highresclock::getFrequency()));
	}
};
#else
typedef std::chrono::high_resolution_clock HighResClock;
#endif

typedef std::chrono::time_point<HighResClock> HighResTimePointType;
