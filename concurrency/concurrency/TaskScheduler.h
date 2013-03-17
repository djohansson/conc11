#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "Task.h"
#include "TaskUtils.h"
#include "Types.h"

#include <concurrent_queue.h>

#include <atomic>
#include <cassert>
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
struct JoinAndSetTupleValueRecursive;

class TaskScheduler : public NonCopyable
{
	typedef Concurrency::concurrent_queue<std::shared_ptr<ITask>> ConcurrentQueueType;
	
public:

	TaskScheduler(unsigned int threadCount = std::max(2U, std::thread::hardware_concurrency()) - 1)
		: m_running(true)
		, m_taskConsumerCount(threadCount)
		, m_updateWaitListTaskEnabled(false)
	{
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
		wait();
		
		// signal threads to exit
		auto p = std::make_shared<std::promise<void>>();
		auto t = std::make_shared<Task<void>>([=, this]
		{
			m_running = false;
			p->set_value();
		}, p->get_future().share(), std::make_shared<TaskEnabler<bool>>(true), TaskPriority::Normal, "killThreads");
		
		m_queue.push(t);
		wakeOne();

		for (auto& t : m_threads)
			t->join(); // will wake all threads due to std::notify_all_at_thread_exit

		assert(m_queue.empty());
	}

	// threadsafe
	void wait()
	{
		// signal wait list update task to return
		m_updateWaitListTaskEnabled = false;

		// join in on tasks until queue is empty and no consumers are running
		waitJoin();

		// update wait list on this thread
		unsigned int n = 0;
		while (!updateWaitList(true))
			n++;

		// wake the appropriate number of worker threads
		if (n > 0)
		{
			if (n > 1)
				wakeAll();
			else
				wakeOne();
		}

		// and join in again.
		waitJoin();

		// done!
	}

	// ReturnType Func(void) w/o dependency. threadsafe
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

	// ReturnType Func(T) with dependency. threadsafe. todo: remove. no point in having this once join works as intended.
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

	// join heterogeneous tasks. threadsafe
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, TaskPriority priority = Normal, const std::string& name = ""*/) const
	{
		return joinHeteroFutures(f0, f1, fn.../*, priority, name*/);
	}

	// join homogeneous tasks. threadsafe
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> join(const FutureContainer& c/*, TaskPriority priority = Normal, const std::string& name = ""*/) const
	{
		return joinHomoFutures(c/*, priority, name*/);
	}

private:

	// threadsafe
	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, T argIsVoid, U fIsVoid, TaskPriority priority, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			trySetFuncResult(*p, f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
		}, p->get_future().share(), priority, name);

		m_waitList.push(t);
		createUpdateWaitListTask();
		
		return t;
	}

	// threadsafe
	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, U argIsVoid, V fIsVoid, X argIsAssignable, TaskPriority priority, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			pollDependencyAndCallOrResubmit(p, fut, f, dependency, argIsVoid, fIsVoid, argIsAssignable, priority, name);
		}, fut, dependency->enabler(), priority, name);

		m_waitList.push(t);
		createUpdateWaitListTask();

		return t;
	}

	// threadsafe
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> joinHeteroFutures(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, TaskPriority priority, const std::string& name*/) const
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		std::array<std::shared_ptr<ITaskEnabler>, 2+sizeof...(Args)> enablers;
		ArraySetValueRecursive<(2+sizeof...(Args))>::invoke(enablers, f0->enabler(), f1->enabler(), fn->enabler()...);

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			ReturnType ret;
			JoinAndSetTupleValueRecursive<(2+sizeof...(Args))>::invoke(ret, f0->future(), f1->future(), fn->future()...);
			trySetResult(*p, std::move(ret));
		}, p->get_future().share(), std::make_shared<TaskEnabler<std::shared_ptr<ITaskEnabler>, 2+sizeof...(Args)>>(std::move(enablers)), TaskPriority::Normal, "joinHeteroFutures");

		m_waitList.push(t);
		createUpdateWaitListTask();

		return t;
	}

	// threadsafe
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> joinHomoFutures(const FutureContainer& c/*, TaskPriority priority, const std::string& name*/) const
	{
		typedef std::vector<typename FutureContainer::value_type::element_type::ReturnType> ReturnType;

		std::vector<std::shared_ptr<ITaskEnabler>> enablers;
		for (auto&n : c)
			enablers.push_back(n->enabler());

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto t = std::make_shared<Task<ReturnType>>([=]
		{
			ReturnType ret;
			ret.reserve(c.size());

			for (auto&n : c)
				ret.push_back(n->future().get());

			trySetResult(*p, std::move(ret));
		}, p->get_future().share(), std::make_shared<DynamicTaskEnabler>(std::move(enablers)), TaskPriority::Normal, "joinHomoFutures");

		m_waitList.push(t);
		createUpdateWaitListTask();

		return t;
	}

	// threadsafe
	void threadMain() const
	{
		while (m_running)
		{
			// process main queue or sleep
			std::shared_ptr<ITask> t;
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

	// threadsafe
	void waitJoin() 
	{
		// join in on tasks until queue is empty and no consumers are running
		while (m_taskConsumerCount > 0)
		{
			std::shared_ptr<ITask> t;
			while (m_queue.try_pop(t))
			{
				assert(t.get() && *t);
				(*t)();
				std::this_thread::yield();
			}

			std::this_thread::yield();
		}
	}

	// threadsafe
	inline void wakeOne() const
	{
		std::unique_lock<std::mutex> lock(m_mutex);		
		m_cv.notify_one();
	}

	// threadsafe
	inline void wakeAll() const
	{
		std::unique_lock<std::mutex> lock(m_mutex);		
		m_cv.notify_all();
	}

	// threadsafe
	template<typename Func, typename T, typename U, typename V, typename X, typename Y>
	void pollDependencyAndCallOrResubmit(std::shared_ptr<std::promise<T>> p, std::shared_future<T> fut, Func f, const std::shared_ptr<Task<U>>& dependency, V argIsVoid, X fIsVoid, Y argIsAssignable, TaskPriority priority, const std::string& name) const
	{
		// The implementations are encouraged to detect the case when valid == false before the call
		// and throw a future_error with an error condition of future_errc::no_state. 
		// http://en.cppreference.com/w/cpp/thread/future/wait_for
		if (dependency->future().valid())
		{
			// std::future_status::deferred is broken in VS2012, therefore we need to do this test here.
			if (dependency->future()._Is_ready())
			{
				trySetFuncResult(*p, f, dependency->future(), argIsVoid, fIsVoid, argIsAssignable);
				return;
			}

			switch (dependency->future().wait_for(std::chrono::seconds(0)))
			{
			default:
			case std::future_status::ready:
				trySetFuncResult(*p, f, dependency->future(), argIsVoid, fIsVoid, argIsAssignable);
				break;
			case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
			case std::future_status::timeout:
				// "Just when I thought I was out... they pull me back in." - Michael Corleone
				m_queue.push(std::make_shared<Task<T>>([=]
				{
					pollDependencyAndCallOrResubmit(p, fut, f, dependency, argIsVoid, fIsVoid, argIsAssignable, priority, name);
				}, fut, dependency->enabler(), priority, name));
				break;
			}
		}
		else
		{
			throw std::future_error(std::future_errc::no_state, "broken dependency");
		}
	}

	void updateWaitListAndResubmit(std::shared_ptr<std::promise<void>> p, std::shared_future<void> fut, std::shared_ptr<ITaskEnabler> e) const
	{
		updateWaitList();

		bool expected = false;
		if (m_updateWaitListTaskEnabled.compare_exchange_strong(expected, false))
		{
			trySetResult(*p);
			return;
		}

		m_queue.push(std::make_shared<Task<void>>([=]
		{
			updateWaitListAndResubmit(p, fut, e);
		}, fut, e, TaskPriority::Normal, "updateWaitList"));
	}

	// threadsafe
	void createUpdateWaitListTask() const
	{
		bool expected = false;
		if (m_updateWaitListTaskEnabled.compare_exchange_strong(expected, true))
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto e = std::make_shared<TaskEnabler<bool>>(true);
			updateWaitListAndResubmit(p, fut, e);
			wakeOne();
		}
	}

	// threadsafe
	bool updateWaitList(bool flush = false) const
	{
		// process one element in wait list
		std::shared_ptr<ITask> t;
		if (m_waitList.try_pop(t))
		{
			assert(t.get());
			if (*t || flush)
				m_queue.push(t);
			else
				m_waitList.push(t);
		}

		return m_waitList.empty();
	}

	// fully concurrent state
	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
	mutable ConcurrentQueueType m_queue;
	mutable std::atomic_uint32_t m_taskConsumerCount;
	mutable std::atomic<bool> m_running;
	mutable ConcurrentQueueType m_waitList;
	mutable std::atomic<bool> m_updateWaitListTaskEnabled;

	// main thread only state
	std::vector<std::shared_ptr<std::thread>> m_threads;
};

// todo: tidy up, generalize and move somewhere
template<unsigned int N>
struct JoinAndSetTupleValueRecursive
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

template<unsigned int N>
struct ArraySetValueRecursive
{
public:

	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(T& ret, const Args&... fn)
	{
		ArraySetValueRecursiveImpl<I>::invoke(ret, fn...);
	}

private:

	template<unsigned int I>
	struct ArraySetValueRecursiveImpl
	{
		template<typename U, typename V, typename... X>
		inline static void invoke(U& ret, const V& f0, const X&... fn)
		{
			ret[I] = f0;
			ArraySetValueRecursiveImpl<I+1>::invoke(ret, fn...);
		}
	};

	template<>
	struct ArraySetValueRecursiveImpl<N>
	{
		template<typename U, typename... X>
		inline static void invoke(U&, const X&...)
		{
		}
	};
};

} // namespace conc11
