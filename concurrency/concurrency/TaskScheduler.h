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
		, m_schedulerTaskEnabled(false)
		, m_killSchedulerTask(false)
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
		auto fut = p->get_future().share();
		auto e = std::make_shared<TaskEnabler<bool>>(true);
		auto t = std::make_shared<Task<void>>("killThreads");
		auto tf = std::function<void()>([=, this]
		{
			m_running = false;
			p->set_value();
			t->setStatus(Done);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));
		
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
		m_killSchedulerTask = false;

		// update wait list on this thread
		while (!scheduleOneOrRequeueInWaitList(true));
		
		// wake all
		wakeAll();

		// and join in.
		waitJoin();
	}

	// ReturnType Func(void) w/o dependency. threadsafe
	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f = Normal, const std::string& name = "") const
	{
		return createTaskWithoutDependency(
			f,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			name);
	}

	// ReturnType Func(T) with dependency. threadsafe. todo: remove. no point in having this once join works as intended.
	template<typename Func, typename T>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency = Normal, const std::string& name = "") const
	{
		return createTaskWithDependency(
			f,
			dependency,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			std::is_assignable<T, FunctionTraits<Func>::Arg<0>::Type>(),
			name);
	}

	// join heterogeneous tasks. threadsafe
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/* = Normal, const std::string& name = ""*/) const
	{
		return joinHeteroFutures(f0, f1, fn.../*, name*/);
	}

	// join homogeneous tasks. threadsafe
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> join(const FutureContainer& c/* = Normal, const std::string& name = ""*/) const
	{
		return joinHomoFutures(c/*, name*/);
	}

private:

	// threadsafe
	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, T argIsVoid, U fIsVoid, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<TaskEnabler<bool>>(false);
		auto t = std::make_shared<Task<ReturnType>>(name);
		auto tf = std::function<void()>([=]
		{
			trySetFuncResult(*p, f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
			t->setStatus(Done);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();
		
		return t;
	}

	// threadsafe
	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, U argIsVoid, V fIsVoid, X argIsAssignable, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = dependency->getEnabler();
		auto t = std::make_shared<Task<ReturnType>>(name);
		auto tf = std::function<void()>([=]
		{
			pollDependencyAndCallOrResubmit(
				f,
				t,
				dependency,
				argIsVoid,
				fIsVoid,
				argIsAssignable);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

		return t;
	}

	// threadsafe
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> joinHeteroFutures(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, const std::string& name*/) const
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		std::array<std::shared_ptr<ITaskEnabler>, 2+sizeof...(Args)> enablers;
		ArraySetValueRecursive<(2+sizeof...(Args))>::invoke(enablers, f0->getEnabler(), f1->getEnabler(), fn->getEnabler()...);

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<TaskEnabler<std::shared_ptr<ITaskEnabler>, 2+sizeof...(Args)>>(std::move(enablers));
		auto t = std::make_shared<Task<ReturnType>>("joinHeteroFutures");
		auto tf = std::function<void()>([=]
		{
			ReturnType ret;
			JoinAndSetTupleValueRecursive<(2+sizeof...(Args))>::invoke(ret, f0->getFuture(), f1->getFuture(), fn->getFuture()...);
			trySetResult(*p, std::move(ret));
			t->setStatus(Done);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

		return t;
	}

	// threadsafe
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> joinHomoFutures(const FutureContainer& c/*, const std::string& name*/) const
	{
		typedef std::vector<typename FutureContainer::value_type::element_type::ReturnType> ReturnType;

		std::vector<std::shared_ptr<ITaskEnabler>> enablers;
		for (auto&n : c)
			enablers.push_back(n->getEnabler());

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<DynamicTaskEnabler>(std::move(enablers));
		auto t = std::make_shared<Task<ReturnType>>("joinHomoFutures");
		auto tf = std::function<void()>([=]
		{
			ReturnType ret;
			ret.reserve(c.size());

			for (auto&n : c)
				ret.push_back(n->getFuture().get());

			trySetResult(*p, std::move(ret));
			t->setStatus(Done);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

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
	void pollDependencyAndCallOrResubmit(Func f, std::shared_ptr<Task<T>> t, const std::shared_ptr<Task<U>>& dependency, V argIsVoid, X fIsVoid, Y argIsAssignable) const
	{
		auto arg = dependency->getFuture();

		// The implementations are encouraged to detect the case when valid == false before the call
		// and throw a future_error with an error condition of future_errc::no_state. 
		// http://en.cppreference.com/w/cpp/thread/future/wait_for
		if (arg.valid())
		{
			// std::future_status::deferred is broken in VS2012, therefore we need to do this test here.
			if (arg._Is_ready())
			{
				trySetFuncResult(*(t->getPromise()), f, arg, argIsVoid, fIsVoid, argIsAssignable);
				t->setStatus(Done);
				return;
			}

			switch (arg.wait_for(std::chrono::seconds(0)))
			{
			default:
			case std::future_status::ready:
				trySetFuncResult(*(t->getPromise()), f, arg, argIsVoid, fIsVoid, argIsAssignable);
				t->setStatus(Done);
				break;
			case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
			case std::future_status::timeout:
				{
					t->setStatus(ScheduledPolling);
					m_queue.push(t);
				}
				break;
			}
		}
		else
		{
			throw std::future_error(std::future_errc::no_state, "broken dependency");
		}
	}

	void schedulerUpdateTask(std::shared_ptr<Task<void>> t) const
	{
		bool expected = scheduleOneOrRequeueInWaitList(m_killSchedulerTask);
		if (m_schedulerTaskEnabled.compare_exchange_strong(expected, false))
		{
			trySetResult(*(t->getPromise()));
			t->setStatus(Done);
			return;
		}
		
		t->setStatus(ScheduledPolling);
		m_queue.push(t);
	}

	// threadsafe
	void createSchedulerUpdateTask() const
	{
		bool expected = false;
		if (m_schedulerTaskEnabled.compare_exchange_strong(expected, true))
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto e = std::make_shared<TaskEnabler<bool>>(true);
			auto t = std::make_shared<Task<void>>("schedulerUpdate");
			auto tf = std::function<void()>([=]
			{
				schedulerUpdateTask(t);
			});
			t->movePromise(std::move(p));
			t->moveFuture(std::move(fut));
			t->moveEnabler(std::move(e));
			t->setFunction(std::move(tf));
			
			m_queue.push(t);

			wakeOne();
		}
	}

	// threadsafe
	bool scheduleOneOrRequeueInWaitList(bool forceSchedule = false) const
	{
		// process one element in wait list
		std::shared_ptr<ITask> t;
		if (m_waitList.try_pop(t))
		{
			assert(t.get());
			if (*t || forceSchedule)
			{
				t->setStatus(ScheduledOnce);
				m_queue.push(t);
			}
			else
			{
				m_waitList.push(t);
			}
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
	mutable std::atomic<bool> m_schedulerTaskEnabled;
	mutable std::atomic<bool> m_killSchedulerTask;

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
