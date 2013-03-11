#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "Task.h"
#include "TaskFuture.h"
#include "TaskFutureUtils.h"
#include "Types.h"

#include <assert.h>
#include <concurrent_queue.h>

#include <atomic>
#include <exception>
#include <future>
#include <functional>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace conc11
{

template<unsigned int N>
struct JoinAndSetValueRecursive;

class TaskScheduler : public NonCopyable
{
	typedef Concurrency::concurrent_queue<Task> QueueType;

public:

	TaskScheduler(unsigned int threadCount = std::max(1U, std::thread::hardware_concurrency()))
		: m_running(true)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		for (unsigned int i = 0; i < threadCount; i++)
		{
			m_threads.push_back(std::make_shared<std::thread>(std::thread([this]
			{
				try
				{
					threadMain();
				}
				catch (const std::future_error& e)
				{
					if (e.code() == std::make_error_code(std::future_errc::broken_promise))
						__debugbreak();
					else if (e.code() == std::make_error_code(std::future_errc::future_already_retrieved))
						__debugbreak();
					else if (e.code() == std::make_error_code(std::future_errc::promise_already_satisfied))
						__debugbreak();
					else if (e.code() == std::make_error_code(std::future_errc::no_state))
						__debugbreak();
					else
						__debugbreak();
				}
				catch (...)
				{
					__debugbreak();
				}

				std::notify_all_at_thread_exit(m_cv, std::move(std::unique_lock<std::mutex>(m_mutex)));
			})));
		}
	}

	~TaskScheduler()
	{
		waitJoin();

		// signal threads to exit
		m_queue.push([this]
		{
			m_running = false;
		});

		wakeOne();

		for (auto& t : m_threads)
			t->join(); // will wake all threads due to std::notify_all_at_thread_exit

		assert(m_queue.empty());
	}

	void waitJoin() const
	{
		// join in on tasks until queue is empty and threads are idle
		while (m_sleepingThreadCount < m_threads.size())
		{
			Task t;
			while (m_queue.try_pop(t))
			{
				t();
				std::this_thread::yield();
			}
			std::this_thread::yield();
		}
	}

	// ReturnType Func(void) w/o dependency.
	template<typename Func>
	auto createTask(Func f) const -> TaskFuture<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>
	{
		return createTaskWithoutDependency(
			f,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>());
	}

	// ReturnType Func(T) with dependency.
	template<typename Func, typename T>
	auto createTask(Func f, const TaskFuture<T>& dependency) const -> TaskFuture<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>
	{
		return createTaskWithDependency(
			f,
			dependency,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			std::is_assignable<T, FunctionTraits<Func>::Arg<0>::Type>());
	}

	// join heterogeneous futures
	template<typename T, typename U, typename... Args>
	auto join(const TaskFuture<T>& f0, const TaskFuture<U>& f1, const TaskFuture<Args>&... fn) const -> TaskFuture<std::tuple<T, U, Args...>>
	{
		return joinHeteroFutures(f0, f1, fn...);
	}

	// join homogeneous futures
	template<typename FutureContainer>
	auto join(const FutureContainer& c) const -> TaskFuture<std::vector<typename FutureContainer::value_type::ReturnType>>
	{
		return joinHomoFutures(c);
	}

private:

	template<typename Func, typename T, typename U>
	auto createTaskWithoutDependency(Func f, T argIsVoid, U fIsVoid) const -> TaskFuture<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = TaskFuture<ReturnType>(std::move(p->get_future()), *this);

		m_queue.push([=]
		{
			trySetFuncResult(*p, f, TaskFuture<UnitType>(*this), argIsVoid, fIsVoid, std::false_type());
		});

		wakeOne();

		return fut;
	}

	template<typename Func, typename T, typename U, typename V, typename X>
	auto createTaskWithDependency(Func f, const TaskFuture<T>& dependency, U argIsVoid, V fIsVoid, X argIsAssignable) const -> TaskFuture<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = TaskFuture<ReturnType>(std::move(p->get_future()), *this);

		m_queue.push([=]
		{
			pollDependencyAndCallOrResubmit(f, p, dependency, argIsVoid, fIsVoid, argIsAssignable);
		});

		wakeOne();

		return fut;
	}

	template<typename T, typename U, typename... Args>
	auto joinHeteroFutures(const TaskFuture<T>& f0, const TaskFuture<U>& f1, const TaskFuture<Args>&... fn) const -> TaskFuture<std::tuple<T, U, Args...>>
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = TaskFuture<ReturnType>(std::move(p->get_future()), *this);

		m_queue.push([=]
		{
			ReturnType ret;

			JoinAndSetValueRecursive<(2+sizeof...(Args))>::invoke(ret, f0, f1, fn...);

			trySetResult(*p, std::move(ret));
		});

		wakeOne();

		return fut;
	}

	template<typename FutureContainer>
	auto joinHomoFutures(const FutureContainer& c) const -> TaskFuture<std::vector<typename FutureContainer::value_type::ReturnType>>
	{
		typedef typename FutureContainer::value_type::ReturnType ElementType;
		typedef std::vector<ElementType> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = TaskFuture<ReturnType>(std::move(p->get_future()), *this);

		m_queue.push([=]
		{
			ReturnType ret;
			ret.reserve(c.size());

			for (auto&n : c)
				ret.push_back(n.get());

			trySetResult(*p, std::move(ret));
		});

		wakeOne();

		return fut;
	}

	void threadMain() const
	{
		Task t;
		while (m_running)
		{
			if (m_queue.try_pop(t))
			{
				t();
				std::this_thread::yield();
			}
			else
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_sleepingThreadCount++;
				m_cv.wait(lock);
				m_sleepingThreadCount--;
			}
		}
	}

	inline void wakeOne() const
	{
		std::unique_lock<std::mutex> lock(m_mutex);		
		m_cv.notify_one();
	}

	inline void wakeAll() const
	{
		std::unique_lock<std::mutex> lock(m_mutex);		
		m_cv.notify_all();
	}

	template<typename Func, typename T, typename U, typename V, typename X, typename Y>
	bool pollDependencyAndCallOrResubmit(Func f, std::shared_ptr<std::promise<T>> p, const TaskFuture<U>& dependency, V argIsVoid, X fIsVoid, Y argIsAssignable) const
	{
		// std::future_status::deferred is broken in VS2012, therefore we need to do this test here.
		if (dependency.ready())
		{
			return trySetFuncResult(*p, f, dependency, argIsVoid, fIsVoid, argIsAssignable);
		}

		// The implementations are encouraged to detect the case when valid == false before the call
		// and throw a future_error with an error condition of future_errc::no_state. 
		// http://en.cppreference.com/w/cpp/thread/future/wait_for
		if (dependency.valid())
		{
			switch (dependency.wait_for(std::chrono::seconds(0)))
			{
			case std::future_status::ready:
				return trySetFuncResult(*p, f, dependency, argIsVoid, fIsVoid, argIsAssignable);
			case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
			case std::future_status::timeout:
				// "Just when I thought I was out... they pull me back in." - Michael Corleone
				m_queue.push([=]
				{
					pollDependencyAndCallOrResubmit(f, p, dependency, argIsVoid, fIsVoid, argIsAssignable);
				});
				break;
			}
		}
		else
		{
			throw std::future_error(std::future_errc::no_state, "broken dependency");
		}

		return false;
	}

	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
	mutable QueueType m_queue;
	mutable std::vector<std::shared_ptr<std::thread>> m_threads;
	mutable std::atomic_int_fast16_t m_sleepingThreadCount;
	mutable std::atomic<bool> m_running;
};

// todo: tidy up, generalize and move somewhere
template<unsigned int N>
struct JoinAndSetValueRecursive
{
public:

	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(T& ret, const Args&... fn)
	{
		JoinAndSetValueRecursiveImpl<I>::invoke(ret, fn...);
	}

private:

	template<unsigned int I>
	struct JoinAndSetValueRecursiveImpl
	{
		template<typename U, typename V, typename... X>
		inline static void invoke(U& ret, const V& f0, const X&... fn)
		{
			std::get<I>(ret) = f0.get();
			JoinAndSetValueRecursiveImpl<I+1>::invoke(ret, fn...);
		}
	};

	template<>
	struct JoinAndSetValueRecursiveImpl<N>
	{
		template<typename U, typename... X>
		inline static void invoke(U&, const X&...)
		{
		}
	};
};

} // namespace conc11
