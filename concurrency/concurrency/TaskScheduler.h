#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "Task.h"
#include "TaskUtils.h"
#include "Types.h"

#include <assert.h>
#include <concurrent_queue.h>

#include <atomic>
#include <exception>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace conc11
{

std::mutex g_coutMutex; // TEMP!!!

template<unsigned int N>
struct JoinAndSetValueRecursive;

class TaskScheduler : public NonCopyable
{
	typedef Concurrency::concurrent_queue<std::shared_ptr<ITask>> QueueType;

public:

	TaskScheduler(unsigned int threadCount = std::max(2U, std::thread::hardware_concurrency()) - 1)
		: m_running(true)
		, m_taskConsumerCount(threadCount)
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
					std::unique_lock<std::mutex> lock(g_coutMutex);

					std::cout << std::endl << "Exception caught in TaskScheduler thread " << std::this_thread::get_id() << ": ";

					if (e.code() == std::make_error_code(std::future_errc::broken_promise))
						std::cout << e.what() << std::endl;
					else if (e.code() == std::make_error_code(std::future_errc::future_already_retrieved))
						std::cout << e.what() << std::endl;
					else if (e.code() == std::make_error_code(std::future_errc::promise_already_satisfied))
						std::cout << e.what() << std::endl;
					else if (e.code() == std::make_error_code(std::future_errc::no_state))
						std::cout << e.what() << std::endl;
					else
						std::cout << e.what() << std::endl;
				}
				catch (...)
				{
					std::cout << "unhandled exception" << std::endl;
				}

				m_taskConsumerCount--;

				std::notify_all_at_thread_exit(m_cv, std::move(std::unique_lock<std::mutex>(m_mutex)));
			})));
		}
	}

	~TaskScheduler()
	{
		waitJoin(true);

		// signal threads to exit
		auto p = std::make_shared<std::promise<void>>();
		auto t = std::make_shared<Task<void>>([=, this]
		{
			m_running = false;
			p->set_value();
		}, std::move(p->get_future()), TaskPriority::Normal, "killThreads");
		
		m_queue.push(t);
		wakeOne();

		for (auto& t : m_threads)
			t->join(); // will wake all threads due to std::notify_all_at_thread_exit

		assert(m_queue.empty());
	}

	void waitJoin(bool enableAllDeferred = false) const
	{
		// join in on tasks until queue is empty and no consumers are running
		std::shared_ptr<ITask> t;
		while (m_taskConsumerCount > 0)
		{
			// process deferred queue and add enabled jobs to main queue
			QueueType deferredQueue;
			while (m_deferredQueue.try_pop(t))
			{
				assert(t.get());
				if (*t || enableAllDeferred)
					m_queue.push(t);
				else
					deferredQueue.push(t);
			}
			// push back tasks in deferred queue
			while (m_deferredQueue.try_pop(t))
			{
				assert(t.get());
				m_deferredQueue.push(t);
			}

			while (m_queue.try_pop(t))
			{
				assert(t.get() && *t);
				(*t)();
				std::this_thread::yield();
			}
			std::this_thread::yield();
		}
	}

	// ReturnType Func(void) w/o dependency.
	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, TaskPriority priority = Normal, const std::string& name = "") const
	{
		return createTaskWithoutDependency(
			f,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			priority,
			name);
	}

	// ReturnType Func(T) with dependency.
	template<typename Func, typename T>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency, TaskPriority priority = Normal, const std::string& name = "") const
	{
		return createTaskWithDependency(
			f,
			dependency,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			std::is_assignable<T, FunctionTraits<Func>::Arg<0>::Type>(),
			priority,
			name);
	}

	// join heterogeneous futures
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, TaskPriority priority = Normal, const std::string& name = ""*/) const
	{
		return joinHeteroFutures(f0, f1, fn.../*, priority, name*/);
	}

	// join homogeneous futures
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> join(const FutureContainer& c/*, TaskPriority priority = Normal, const std::string& name = ""*/) const
	{
		return joinHomoFutures(c/*, priority, name*/);
	}

private:

	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, T argIsVoid, U fIsVoid, TaskPriority priority, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			trySetFuncResult(*p, f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
		}, std::move(p->get_future()), priority, name);

		m_deferredQueue.push(t);
		wakeOne();

		return t;
	}

	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, U argIsVoid, V fIsVoid, X argIsAssignable, TaskPriority priority, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future();
		auto t = std::make_shared<Task<ReturnType>>([=, &fut]
		{
			pollDependencyAndCallOrResubmit(p, fut, f, dependency->getFuture(), argIsVoid, fIsVoid, argIsAssignable, priority, name);
		}, std::move(fut), dependency->getEnabler(), priority, name);

		/*auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			trySetFuncResult(*p, f, dependency->getFuture(), argIsVoid, fIsVoid, argIsAssignable);
		}, std::move(p->get_future()), dependency->getEnabler(), priority, name);*/

		m_queue.push(t);
		wakeOne();

		return t;
	}

	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> joinHeteroFutures(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, TaskPriority priority, const std::string& name*/) const
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			ReturnType ret;
			JoinAndSetValueRecursive<(2+sizeof...(Args))>::invoke(ret, f0->getFuture(), f1->getFuture(), fn->getFuture()...);
			trySetResult(*p, std::move(ret));
		}, std::move(p->get_future()), std::make_shared<bool>(true) /* until I figure out how to make an enabler for N dependencies*/ , TaskPriority::Normal, "joinHeteroFutures");

		m_queue.push(t);
		wakeOne();

		return t;
	}

	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> joinHomoFutures(const FutureContainer& c/*, TaskPriority priority, const std::string& name*/) const
	{
		typedef std::vector<typename FutureContainer::value_type::element_type::ReturnType> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			ReturnType ret;
			ret.reserve(c.size());

			for (auto&n : c)
				ret.push_back(n->getFuture().get());

			trySetResult(*p, std::move(ret));
		}, std::move(p->get_future()), std::make_shared<bool>(true) /* until I figure out how to make an enabler for N dependencies*/, TaskPriority::Normal, "joinHomoFutures");

		m_queue.push(t);
		wakeOne();

		return t;
	}

	void threadMain() const
	{
		std::shared_ptr<ITask> t;
		while (m_running)
		{
			// process deferred queue and add enabled jobs to main queue
			QueueType deferredQueue;
			while (m_deferredQueue.try_pop(t))
			{
				assert(t.get());
				if (*t)
					m_queue.push(t);
				else
					deferredQueue.push(t);
			}
			// push back tasks in deferred queue
			while (m_deferredQueue.try_pop(t))
			{
				assert(t.get());
				m_deferredQueue.push(t);
			}

			// process main queue or sleep
			if (m_queue.try_pop(t))
			{
				assert(t.get() && *t);
				(*t)();
				std::this_thread::yield();
			}
			else
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_taskConsumerCount--;
				m_cv.wait(lock);
				m_taskConsumerCount++;
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
	void pollDependencyAndCallOrResubmit(std::shared_ptr<std::promise<T>> p, std::future<T>& fut, Func f, const std::shared_future<U>& dependency, V argIsVoid, X fIsVoid, Y argIsAssignable, TaskPriority priority, const std::string& name) const
	{
		// The implementations are encouraged to detect the case when valid == false before the call
		// and throw a future_error with an error condition of future_errc::no_state. 
		// http://en.cppreference.com/w/cpp/thread/future/wait_for
		if (dependency.valid())
		{
			// std::future_status::deferred is broken in VS2012, therefore we need to do this test here.
			if (dependency._Is_ready())
			{
				trySetFuncResult(*p, f, dependency, argIsVoid, fIsVoid, argIsAssignable);
				return;
			}

			switch (dependency.wait_for(std::chrono::seconds(0)))
			{
			default:
			case std::future_status::ready:
				trySetFuncResult(*p, f, dependency, argIsVoid, fIsVoid, argIsAssignable);
				break;
			case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
			case std::future_status::timeout:
				// "Just when I thought I was out... they pull me back in." - Michael Corleone
				m_queue.push(std::make_shared<Task<T>>([=, &fut]
				{
					pollDependencyAndCallOrResubmit(p, fut, f, dependency, argIsVoid, fIsVoid, argIsAssignable, priority, name);
				}, std::move(fut), priority, name));
				break;
			}
		}
		else
		{
			throw std::future_error(std::future_errc::no_state, "broken dependency");
		}
	}

	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
	mutable QueueType m_queue;
	mutable QueueType m_deferredQueue;
	mutable std::vector<std::shared_ptr<std::thread>> m_threads;
	mutable std::atomic_int_fast16_t m_taskConsumerCount;
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
