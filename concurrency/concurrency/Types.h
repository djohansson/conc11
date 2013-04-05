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
template<typename KeyType, typename ElementType, typename HashType = std::tr1::hash<KeyType>>
class ConcurrentUnorderedMapType : public Concurrency::concurrent_unordered_map<KeyType, ElementType, HashType> { };
template<typename KeyType, typename ElementType, typename HashType = std::tr1::hash<KeyType>>
class ConcurrentUnorderedMultiMapType : public Concurrency::concurrent_unordered_multimap<KeyType, ElementType, HashType> { };
template<typename T>
class ConcurrentQueueType : public Concurrency::concurrent_queue<T> { };
template<typename T>
class ConcurrentVectorType : public Concurrency::concurrent_vector<T> { };
#else
template<typename KeyType, typename ElementType, typename HashType = tbb::tbb_hash<KeyType>>
class ConcurrentUnorderedMapType : public tbb::concurrent_unordered_map<KeyType, ElementType, HashType> { };
template<typename KeyType, typename ElementType, typename HashType = tbb::tbb_hash<KeyType>>
class ConcurrentUnorderedMultiMapType : public tbb::concurrent_unordered_multimap<KeyType, ElementType, HashType> { };
template<typename T>
class ConcurrentQueueType : public tbb::strict_ppl::concurrent_queue<T> { };
template<typename T>
class ConcurrentVectorType : public tbb::concurrent_vector<T> { };
#endif

} // namespace conc11
