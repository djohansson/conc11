#pragma once

#include "FunctionTraits.h"
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

enum TaskRunMode
{
	TrmAsync,
	TrmSyncJoin,
};

std::mutex g_coutMutex; // TEMP!!!

template<unsigned int N>
struct JoinAndSetTupleValueRecursive;

class TaskScheduler
{
	typedef Concurrency::concurrent_queue<std::shared_ptr<TaskBase>> ConcurrentQueueType;
	
public:

	TaskScheduler(unsigned int threadCount = std::max(2U, std::thread::hardware_concurrency()) - 1)
		: m_running(true)
		, m_taskConsumerCount(threadCount)
		, m_schedulerTaskEnabled(false)
		, m_flushWaitList(false)
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
		enableAllAndSync();

		// signal threads to exit
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto e = std::make_shared<TaskEnabler<bool>>(true);
			auto t = std::make_shared<Task<void>>("killThreads");
			std::weak_ptr<Task<void>> tw = t;
			auto tf = std::function<void()>([this, tw]
			{
				if (auto t = tw.lock())
				{
					m_running = false;
					t->getPromise()->set_value();
					t->setStatus(TsDone);
				}
				else
				{
					assert(false);
				}
			});
			t->movePromise(std::move(p));
			t->moveFuture(std::move(fut));
			t->moveEnabler(std::move(e));
			t->moveFunction(std::move(tf));

			m_queue.push(t);
			wakeOne();
		}

		// join threads
		for (auto& t : m_threads)
			t->join(); // will wake all threads due to std::notify_all_at_thread_exit

		assert(m_queue.empty());
		assert(TaskBase::getInstanceCount() == 0);
	}

	// ReturnType Func(void) w/o dependency
	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f = Normal, const std::string& name = "") const
	{
		return createTaskWithoutDependency(
			f,
			std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
			std::is_void<FunctionTraits<Func>::ReturnType>(),
			name);
	}

	// ReturnType Func(T) with dependencies
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

	// join heterogeneous tasks
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn) const
	{
		return joinHeteroFutures(f0, f1, fn...);
	}

	// join homogeneous tasks
	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> join(const FutureContainer& c) const
	{
		return joinHomoFutures(c);
	}

	// execute task chain
	template<typename T>
	void run(const std::shared_ptr<Task<T>>& t, TaskRunMode mode = TrmAsync) const
	{
		assert(!(*t->getEnabler()));

		t->getEnabler()->enable();

		if (mode == TrmSyncJoin)
			waitJoin(t);
	}

private:

	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, T argIsVoid, U fIsVoid, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<TaskEnabler<bool>>(false);
		auto t = std::make_shared<Task<ReturnType>>(name);
		std::weak_ptr<Task<ReturnType>> tw = t;
		auto tf = std::function<void()>([tw, f, argIsVoid, fIsVoid]
		{
			if (auto t = tw.lock())
			{
				trySetFuncResult(*(t->getPromise()), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
				t->setStatus(TsDone);
			}
			else
			{
				assert(false);
			}
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();
		
		return t;
	}

	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, U argIsVoid, V fIsVoid, X argIsAssignable, const std::string& name) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = dependency->getEnabler();
		auto t = std::make_shared<Task<ReturnType>>(name, true);
		std::weak_ptr<Task<ReturnType>> tw = t;
		auto tf = std::function<void()>([this, tw, f, dependency, argIsVoid, fIsVoid, argIsAssignable]
		{
			if (auto t = tw.lock())
			{
				pollDependencyAndCallOrResubmit(
					f,
					t,
					dependency,
					argIsVoid,
					fIsVoid,
					argIsAssignable);
			}
			else
			{
				assert(false);
			}
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

		return t;
	}

	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> joinHeteroFutures(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, const std::string& name*/) const
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		std::array<std::shared_ptr<TaskEnablerBase>, 2+sizeof...(Args)> enablers;
		ArraySetValueRecursive<(2+sizeof...(Args))>::invoke(enablers, f0->getEnabler(), f1->getEnabler(), fn->getEnabler()...);

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<TaskEnabler<std::shared_ptr<TaskEnablerBase>, 2+sizeof...(Args)>>(std::move(enablers));
		auto t = std::make_shared<Task<ReturnType>>("joinHeteroFutures", true);
		std::weak_ptr<Task<ReturnType>> tw = t;
		auto tf = std::function<void()>([this, tw, f0, f1, fn...]
		{
			if (auto t = tw.lock())
			{
				pollDependenciesAndJoinOrResubmit(t, f0, f1, fn...);
			}
			else
			{
				assert(false);
			}
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

		return t;
	}

	template<typename FutureContainer>
	std::shared_ptr<Task<std::vector<typename FutureContainer::value_type::element_type::ReturnType>>> joinHomoFutures(const FutureContainer& c/*, const std::string& name*/) const
	{
		typedef std::vector<typename FutureContainer::value_type::element_type::ReturnType> ReturnType;

		std::vector<std::shared_ptr<TaskEnablerBase>> enablers;
		for (auto&n : c)
			enablers.push_back(n->getEnabler());

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto e = std::make_shared<DynamicTaskEnabler>(std::move(enablers));
		auto t = std::make_shared<Task<ReturnType>>("joinHomoFutures", true);
		std::weak_ptr<Task<ReturnType>> tw = t;
		auto tf = std::function<void()>([this, tw, c]
		{
			if (auto t = tw.lock())
			{
				pollDependenciesAndJoinOrResubmit(t, c);
			}
			else
			{
				assert(false);
			}
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveEnabler(std::move(e));
		t->moveFunction(std::move(tf));

		m_waitList.push(t);
		createSchedulerUpdateTask();

		return t;
	}

	void threadMain() const
	{
		while (m_running)
		{
			// process main queue or sleep
			std::shared_ptr<TaskBase> t;
			if (m_queue.try_pop(t))
			{
				assert(t.get() && *t);
				(*t)();
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
	void pollDependencyAndCallOrResubmit(Func f, std::shared_ptr<Task<T>> t, const std::shared_ptr<Task<U>>& dependency, V argIsVoid, X fIsVoid, Y argIsAssignable) const
	{
		auto arg = dependency->getFuture();

		// The implementations are encouraged to detect the case when valid == false before the call
		// and throw a future_error with an error condition of future_errc::no_state. 
		// http://en.cppreference.com/w/cpp/thread/future/wait_for
		if (arg.valid())
		{
			switch (arg.wait_for(std::chrono::seconds(0)))
			{
			case std::future_status::ready:
				trySetFuncResult(*(t->getPromise()), f, arg, argIsVoid, fIsVoid, argIsAssignable);
				t->setStatus(TsDone);
				break;
			case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
			case std::future_status::timeout:
				if (arg._Is_ready()) // broken std::future_status::deferred temp workaround
				{
					trySetFuncResult(*(t->getPromise()), f, arg, argIsVoid, fIsVoid, argIsAssignable);
					t->setStatus(TsDone);
				}
				else
				{
					t->setStatus(TsScheduledPolling);
					m_queue.push(t);
				}
				break;
			default:
				assert(false);
				break;
			}
		}
		else
		{
			throw std::future_error(std::future_errc::no_state, "broken dependency");
		}
	}

	template<typename T, typename U, typename V, typename... Args>
	void pollDependenciesAndJoinOrResubmit(std::shared_ptr<Task<T>> t, const std::shared_ptr<Task<U>>& f0, const std::shared_ptr<Task<V>>& f1, const std::shared_ptr<Task<Args>>&... fn) const
	{
		std::tuple<U, V, Args...> ret;
		TaskStatus status = JoinAndSetTupleValueRecursive<(2+sizeof...(Args))>::invoke(ret, f0->getFuture(), f1->getFuture(), fn->getFuture()...);

		switch (status)
		{
		case TsDone:
			trySetResult(*(t->getPromise()), std::move(ret));
			t->setStatus(status);
			break;
		case TsScheduledPolling:
			t->setStatus(status);
			m_queue.push(t);
			break;
		default:
			assert(false);
		}
	}

	template<typename T, typename FutureContainer>
	void pollDependenciesAndJoinOrResubmit(std::shared_ptr<Task<T>> t, const FutureContainer& c) const
	{
		T ret;
		ret.reserve(c.size());

		TaskStatus status = TsDone;

		for (auto&n : c)
		{
			auto fut = n->getFuture();

			// The implementations are encouraged to detect the case when valid == false before the call
			// and throw a future_error with an error condition of future_errc::no_state. 
			// http://en.cppreference.com/w/cpp/thread/future/wait_for
			if (fut.valid())
			{
				switch (fut.wait_for(std::chrono::seconds(0)))
				{
				case std::future_status::ready:
					ret.push_back(fut.get());
					continue;
				case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
				case std::future_status::timeout:
					if (fut._Is_ready()) // broken std::future_status::deferred temp workaround
					{
						ret.push_back(fut.get());
						continue;
					}
					status = TsScheduledPolling;
					break;
				default:
					assert(false);
					break;
				}
			}
			else
			{
				throw std::future_error(std::future_errc::no_state, "broken dependency");
			}

			if (status != TsDone)
				break;
		}

		switch (status)
		{
		case TsDone:
			trySetResult(*(t->getPromise()), std::move(ret));
			t->setStatus(status);
			break;
		case TsScheduledPolling:
			t->setStatus(status);
			m_queue.push(t);
			break;
		default:
			assert(false);
		}
	}

	void schedulerUpdateTask(std::shared_ptr<Task<void>> t) const
	{
		bool waitListIsEmpty = scheduleOrRequeueInWaitList(10 * static_cast<unsigned int>(m_threads.size()), m_flushWaitList);
		if (m_schedulerTaskEnabled.compare_exchange_strong(waitListIsEmpty, false))
		{
			trySetResult(*(t->getPromise()));
			t->setStatus(TsDone);
			return;
		}
		
		t->setStatus(TsScheduledPolling);
		m_queue.push(t);
	}

	void createSchedulerUpdateTask() const
	{
		bool expected = false;
		if (m_schedulerTaskEnabled.compare_exchange_strong(expected, true))
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto e = std::make_shared<TaskEnabler<bool>>(true);
			auto t = std::make_shared<Task<void>>("schedulerUpdate", true);
			std::weak_ptr<Task<void>> tw = t;
			auto tf = std::function<void()>([this, tw]
			{
				if (auto t = tw.lock())
				{
					schedulerUpdateTask(t);
				}
				else
				{
					assert(false);
				}
			});
			t->movePromise(std::move(p));
			t->moveFuture(std::move(fut));
			t->moveEnabler(std::move(e));
			t->moveFunction(std::move(tf));
			
			m_queue.push(t);
			wakeOne();
		}
	}

	bool scheduleOrRequeueInWaitList(unsigned int n, bool forceSchedule = false) const
	{
		// process n elements in wait list
		for (unsigned int i = 0; i < n; i++)
		{
			std::shared_ptr<TaskBase> t;
			if (m_waitList.try_pop(t))
			{
				assert(t.get() != nullptr);
				if (*t || forceSchedule)
				{
					t->setStatus(TsScheduledOnce);
					m_queue.push(t);
					wakeOne();
				}
				else
				{
					m_waitList.push(t);
				}
			}
			else
			{
				break;
			}
		}

		return m_waitList.empty();
	}

	template<typename T = void>
	void waitJoin(std::shared_ptr<Task<T>> t = std::shared_ptr<Task<T>>()) const
	{
		// join in on tasks until queue is empty and no consumers are running
		while (m_taskConsumerCount > 0)
		{
			if (t.get() != nullptr)
			{
				auto fut = t->getFuture();
				// The implementations are encouraged to detect the case when valid == false before the call
				// and throw a future_error with an error condition of future_errc::no_state. 
				// http://en.cppreference.com/w/cpp/thread/future/wait_for
				if (fut.valid())
				{
					switch (fut.wait_for(std::chrono::seconds(0)))
					{
					case std::future_status::ready:
						return;
					case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
					case std::future_status::timeout:
						if (fut._Is_ready()) // broken std::future_status::deferred temp workaround
							return;
						break;
					default:
						assert(false);
						break;
					}
				}
			}

			std::shared_ptr<TaskBase> t;
			while (m_queue.try_pop(t))
			{
				assert(t.get() != nullptr && *t);
				(*t)();
			}
		}
	}

	void enableAllAndSync() 
	{
		// flushing the wait list will make the polling wait list update task to stop and return
		m_flushWaitList = false;

		// flush wait list on this thread
		while (!scheduleOrRequeueInWaitList(static_cast<unsigned int>(m_waitList.unsafe_size()), true));

		// and join in.
		waitJoin();
	}

	TaskScheduler(const TaskScheduler&);
	TaskScheduler& operator=(const TaskScheduler&);

	// concurrent state
	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
	mutable ConcurrentQueueType m_queue;
	mutable ConcurrentQueueType m_waitList;
	mutable std::atomic_uint32_t m_taskConsumerCount;
	mutable std::atomic<bool> m_running;
	mutable std::atomic<bool> m_schedulerTaskEnabled;
	mutable std::atomic<bool> m_flushWaitList;

	// main thread only state
	std::vector<std::shared_ptr<std::thread>> m_threads;
};

// todo: tidy up, generalize and move somewhere
template<unsigned int N>
struct JoinAndSetTupleValueRecursive
{
public:

	template <typename T, typename... Args, unsigned int I=0>
	__forceinline static TaskStatus invoke(T& ret, const Args&... fn)
	{
		return JoinAndSetValueRecursiveImpl<I>::invoke(ret, fn...);
	}

private:

	template<unsigned int I>
	struct JoinAndSetValueRecursiveImpl
	{
		template<typename U, typename V, typename... X>
		static TaskStatus invoke(U& ret, const V& f0, const X&... fn)
		{
			// The implementations are encouraged to detect the case when valid == false before the call
			// and throw a future_error with an error condition of future_errc::no_state. 
			// http://en.cppreference.com/w/cpp/thread/future/wait_for
			if (f0.valid())
			{
				switch (f0.wait_for(std::chrono::seconds(0)))
				{
				case std::future_status::ready:
					std::get<I>(ret) = f0.get();
					return JoinAndSetValueRecursiveImpl<I+1>::invoke(ret, fn...);
				case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
				case std::future_status::timeout:
					if (f0._Is_ready()) // broken std::future_status::deferred temp workaround
					{
						std::get<I>(ret) = f0.get();
						return JoinAndSetValueRecursiveImpl<I+1>::invoke(ret, fn...);
					}
					return TsScheduledPolling;
				default:
					assert(false);
					break;
				}
			}
			else
			{
				throw std::future_error(std::future_errc::no_state, "broken dependency");
			}

			return TsInvalid;
		}
	};

	template<>
	struct JoinAndSetValueRecursiveImpl<N>
	{
		template<typename U, typename... X>
		__forceinline static TaskStatus invoke(U&, const X&...)
		{
			return TsDone;
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
