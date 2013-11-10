#pragma once

#include <framework/Bitfields.h>

#if (_MSC_VER >= 1600)
#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>
#include <concurrent_vector.h>
#else
#if defined(USE_TBB)
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_vector.h>
#else
#include <framework/MutexedQueue.h>
#endif
#endif

#include <future>

namespace conc11
{

typedef unsigned char UnitType;
	
typedef Bitfields<8, 8, 8, 8> Color;
enum ColorComponent
{
	CcRed = 0,
	CcGreen = 1,
	CcBlue = 2,
	CcAlpha = 3,
	
	CcCount
};
	
inline static Color createColor(unsigned red, unsigned green, unsigned blue, unsigned alpha)
{
	Color c;
	set<CcRed>(c, red);
	set<CcGreen>(c, green);
	set<CcBlue>(c, blue);
	set<CcAlpha>(c, alpha);
	return c;
}

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
#if defined(USE_TBB)
template<typename T>
class ConcurrentQueueType : public tbb::strict_ppl::concurrent_queue<T> { };
#else
template<typename T>
class ConcurrentQueueType : public MutexedQueue<T> { };
#endif
#endif
	
// Fut Func(Arg)
template<typename Fut, typename Func, typename Arg>
inline static void trySetFuncResult(std::promise<Fut>& p, Func f, const std::shared_future<Arg>& arg, std::false_type /*argIsVoid*/, std::false_type /*fIsVoid*/, std::true_type /*argIsAssignable*/)
{
	try
	{
		p.set_value(f(arg.get()));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}

// void Func(Arg)
template<typename Func, typename Arg>
inline static void trySetFuncResult(std::promise<UnitType>& p, Func f, const std::shared_future<Arg>& arg, std::false_type /*argIsVoid*/, std::true_type /*fIsVoid*/, std::true_type /*argIsAssignable*/) 
{
	try
	{
		f(arg.get());
		p.set_value(UnitType(0));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}

// Fut Func(void)
template<typename Fut, typename Func>
inline static void trySetFuncResult(std::promise<Fut>& p, Func f, const std::shared_future<UnitType>& /*arg*/, std::true_type /*argIsVoid*/, std::false_type /*fIsVoid*/, std::false_type /*argIsAssignable*/) 
{
	try
	{
		p.set_value(f());
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}

// void Func(void)
template<typename Func>
inline static void trySetFuncResult(std::promise<UnitType>& p, Func f, const std::shared_future<UnitType>& /*arg*/, std::true_type /*argIsVoid*/, std::true_type /*fIsVoid*/, std::false_type /*argIsAssignable*/) 
{
	try
	{
		f();
		p.set_value(UnitType(0));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}

template<typename Fut>
inline static void trySetResult(std::promise<Fut>& p, Fut&& val) 
{
	try
	{
		p.set_value(std::forward<Fut>(val));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}

inline static void trySetResult(std::promise<UnitType>& p)
{
	try
	{
		p.set_value(0);
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
	}
}
	
template <unsigned int N>
struct AddDependencyRecursive
{
	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(const T& t, Args&... fn)
	{
		AddDependencyRecursiveImpl<I, (I >= N)>::invoke(t, fn...);
	}
	
private:
	
	template <unsigned int I, bool Terminate>
	struct AddDependencyRecursiveImpl;
	
	template <unsigned int I>
	struct AddDependencyRecursiveImpl<I, false>
	{
		template <typename U, typename V, typename... X>
		static void invoke(const U& t, V& f0, X&... fn)
		{
			t->addDependency();
			f0->addWaiter(t);
			AddDependencyRecursiveImpl<I+1, (I+1 >= N)>::invoke(t, fn...);
		}
	};
	
	template <unsigned int I>
	struct AddDependencyRecursiveImpl<I, true>
	{
		template<typename U, typename... X>
		inline static void invoke(const U&, X&...)
		{
		}
	};
};
	
template <unsigned int N>
struct SetTupleValueRecursive
{
	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(T& ret, const Args&... fn)
	{
		SetTupleValueRecursiveImpl<I, (I >= N)>::invoke(ret, fn...);
	}
	
private:
	
	template <unsigned int I, bool Terminate>
	struct SetTupleValueRecursiveImpl;
	
	template <unsigned int I>
	struct SetTupleValueRecursiveImpl<I, false>
	{
		template <typename U, typename V, typename... X>
		static void invoke(U& ret, const V& f0, const X&... fn)
		{
			std::get<I>(ret) = f0.get();
			SetTupleValueRecursiveImpl<I+1, (I+1 >= N)>::invoke(ret, fn...);
		}
	};
	
	template <unsigned int I>
	struct SetTupleValueRecursiveImpl<I, true>
	{
		template<typename U, typename... X>
		inline static void invoke(U&, const X&...)
		{
		}
	};
};

} // namespace conc11
