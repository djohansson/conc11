#include "HighResClock.h"

#if defined (_MSC_VER)
#include <windows.h>
#endif

namespace highresclock
{

#if defined (_MSC_VER)
CONC11_CORE_API long long getFrequency()
{
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return frequency.QuadPart;
}

CONC11_CORE_API long long getCounter()
{
	LARGE_INTEGER count;
	QueryPerformanceCounter(&count);
	return count.QuadPart;
}
#endif

}
