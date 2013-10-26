#pragma once

#if (_MSC_VER >= 1600)
#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>
#include <concurrent_vector.h>
#elseif defined(USE_TBB)
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_vector.h>
#else
#include "MutexedQueue.h"
#endif

namespace conc11
{

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
#elseif defined(USE_TBB)
template<typename T>
class ConcurrentQueueType : public tbb::strict_ppl::concurrent_queue<T> { };
#else
template<typename T>
class ConcurrentQueueType : public MutexedQueue<T> { };
#endif

} // namespace conc11
