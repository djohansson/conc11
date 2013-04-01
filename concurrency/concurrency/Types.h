#pragma once

#if (_MSC_VER >= 1600)
#include <concurrent_queue.h>
#else
#include <tbb/concurrent_queue.h>
#endif

namespace conc11
{

struct Nil 
{
};

typedef unsigned char UnitType;

template<typename T>
struct VoidToUnitType
{
	typedef T Type;
};

template<>
struct VoidToUnitType<void>
{
	typedef UnitType Type;
};

#if (_MSC_VER >= 1600)
template<typename T>
class ConcurrentQueueType : public Concurrency::concurrent_queue<T> { };
#else
template<typename T>
class ConcurrentQueueType : public tbb::strict_ppl::concurrent_queue<T> { };
#endif

} // namespace conc11
