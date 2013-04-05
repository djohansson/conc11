#pragma once

#if (_MSC_VER >= 1600)
#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>
#include <concurrent_vector.h>
#else
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_vector.h>
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
template<typename KeyType, typename ElementType>
class ConcurrentUnorderedMapType : public Concurrency::concurrent_unordered_map<KeyType, ElementType> { };
template<typename KeyType, typename ElementType>
class ConcurrentUnorderedMultiMapType : public Concurrency::concurrent_unordered_multimap<KeyType, ElementType> { };
template<typename T>
class ConcurrentQueueType : public Concurrency::concurrent_queue<T> { };
template<typename T>
class ConcurrentVectorType : public Concurrency::concurrent_vector<T> { };
#else
template<typename KeyType, typename ElementType>
class ConcurrentUnorderedMapType : public tbb::strict_ppl::concurrent_unordered_map<KeyType, ElementType> { };
template<typename KeyType, typename ElementType>
class ConcurrentUnorderedMultiMapType : public tbb::strict_ppl::concurrent_unordered_multimap<KeyType, ElementType> { };
template<typename T>
class ConcurrentQueueType : public tbb::strict_ppl::concurrent_queue<T> { };
template<typename T>
class ConcurrentVectorType : public tbb::strict_ppl::concurrent_vector<T> { };
#endif

} // namespace conc11
