#pragma once

#include "FunctionTraits.h"
#include "Task.h"
#include "TaskUtils.h"
#include "Types.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
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
	TrmSyncWait,
};

std::mutex g_coutMutex; // TEMP!!!

template<unsigned int N>
struct JoinAndSetTupleValueRecursive;
    
template<unsigned int N>
struct ArraySetValueRecursive;

class TaskScheduler
{	
public:

	TaskScheduler(unsigned int threadCount = std::max(2U, std::thread::hardware_concurrency()) - 1)
		: m_running(true)
		, m_taskConsumerCount(threadCount)
		, m_schedulerTaskEnabled(false)
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
		sync();

		// signal threads to exit
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto t = std::make_shared<Task<void>>("killThreads");
			Task<void>& tref = *t;
			auto tf = std::function<void()>([this, &tref]
			{
				m_running = false;
				tref.getPromise()->set_value();
				tref.setStatus(TsDone);
			});
			t->movePromise(std::move(p));
			t->moveFuture(std::move(fut));
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

	// ReturnType Func(...) w/o dependency
	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::string& name = "") const
	{
        return createTaskWithoutDependency(
			f,
            name,
            std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
			std::is_void<typename FunctionTraits<Func>::ReturnType>());
	}
    
    // ReturnType Func(void) with dependencies
	template<typename Func, typename T>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name = "") const
	{
        return createTaskWithDependency(
            f,
            dependency,
            name,
            std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
            std::is_void<typename FunctionTraits<Func>::ReturnType>(),
            std::is_convertible<T, typename FunctionTraits<Func>::template Arg<0>::Type>());
            //std::is_assignable<T, typename FunctionTraits<Func>::template Arg<0>::Type>()); // does not compile with clang 4.2
	}

	// join heterogeneous tasks
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn) const
	{
		return joinTasks(f0, f1, fn...);
	}

	// join homogeneous tasks
	template<typename TaskContainer>
	std::shared_ptr<Task<std::vector<typename TaskContainer::value_type::element_type::ReturnType>>> join(const TaskContainer& c) const
	{
		return joinTasks(c);
	}

	// execute task chain
	template<typename T>
	void run(std::shared_ptr<Task<T>>& t, TaskRunMode mode = TrmAsync) const
	{
		createSchedulerUpdateTask(enqueue(t, m_waitList));

		if (mode == TrmSyncJoin)
			waitJoin(t);
		else if (mode == TrmSyncWait)
			t->getFuture().wait();
	}

private:

	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, const std::string& name, T argIsVoid, U fIsVoid) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ReturnType>>(name);
		Task<ReturnType>& tref = *t;
		auto tf = std::function<void()>([&tref, f, argIsVoid, fIsVoid]
		{
			trySetFuncResult(*tref.getPromise(), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
			tref.setStatus(TsDone);
		});
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveFunction(std::move(tf));
		
		return t;
	}

	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name, U argIsVoid, V fIsVoid, X argIsAssignable) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ReturnType>>(name, true);
		std::weak_ptr<Task<ReturnType>> tw = t;
		auto tf = std::function<void()>([this, f, tw, dependency, argIsVoid, fIsVoid, argIsAssignable]
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
		t->moveFunction(std::move(tf));
		t->addDependencies(dependency);

		return t;
	}

	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> joinTasks(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn/*, const std::string& name*/) const
	{
		typedef std::tuple<T, U, Args...> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ReturnType>>("joinTasks", true);
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
		t->moveFunction(std::move(tf));
		t->addDependencies(f0, f1, fn...);

		return t;
	}

	template<typename TaskContainer>
	std::shared_ptr<Task<std::vector<typename TaskContainer::value_type::element_type::ReturnType>>> joinTasks(const TaskContainer& c/*, const std::string& name*/) const
	{
		typedef std::vector<typename TaskContainer::value_type::element_type::ReturnType> ReturnType;

		auto p = std::make_shared<std::promise<ReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ReturnType>>("joinTasks", true);
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
		t->moveFunction(std::move(tf));
		t->addDependencies(c);

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
				assert(t.get() != nullptr);
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
	void pollDependencyAndCallOrResubmit(Func f, std::shared_ptr<Task<T>>& t, const std::shared_ptr<Task<U>>& d, V argIsVoid, X fIsVoid, Y argIsAssignable) const
	{
		auto arg = d->getFuture();

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
#if (_MSC_VER >= 1600)
				if (arg._Is_ready()) // broken std::future_status::deferred temp workaround
				{
					trySetFuncResult(*(t->getPromise()), f, arg, argIsVoid, fIsVoid, argIsAssignable);
					t->setStatus(TsDone);
				}
				else
#endif
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
			throw std::future_error(std::future_errc::no_state);
		}
	}

	template<typename T, typename U, typename V, typename... Args>
	void pollDependenciesAndJoinOrResubmit(std::shared_ptr<Task<T>>& t, const std::shared_ptr<Task<U>>& f0, const std::shared_ptr<Task<V>>& f1, const std::shared_ptr<Task<Args>>&... fn) const
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

	template<typename T, typename TaskContainer>
	void pollDependenciesAndJoinOrResubmit(std::shared_ptr<Task<T>>& t, const TaskContainer& c) const
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
#if (_MSC_VER >= 1600)
					if (fut._Is_ready()) // broken std::future_status::deferred temp workaround
					{
						ret.push_back(fut.get());
						continue;
					}
#endif
					status = TsScheduledPolling;
					break;
				default:
					assert(false);
					break;
				}
			}
			else
			{
				throw std::future_error(std::future_errc::no_state);
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

	void schedulerUpdateTask(std::shared_ptr<Task<void>>& t, unsigned int count) const
	{
		bool waitListIsEmpty = schedule(count);
		if (m_schedulerTaskEnabled.compare_exchange_strong(waitListIsEmpty, false))
		{
			trySetResult(*(t->getPromise()));
			t->setStatus(TsDone);
			return;
		}
		
		m_queue.push(t);
	}

	void createSchedulerUpdateTask(unsigned int count) const
	{
		bool expected = false;
		if (m_schedulerTaskEnabled.compare_exchange_strong(expected, true))
		{
			auto p = std::make_shared<std::promise<void>>();
			auto fut = p->get_future().share();
			auto t = std::make_shared<Task<void>>("schedulerUpdate", true);
			std::weak_ptr<Task<void>> tw = t;
			auto tf = std::function<void()>([this, tw, count]
			{
				if (auto t = tw.lock())
				{
					schedulerUpdateTask(t, count);
				}
				else
				{
					assert(false);
				}
			});
			t->movePromise(std::move(p));
			t->moveFuture(std::move(fut));
			t->moveFunction(std::move(tf));
			
			t->setStatus(TsScheduledPolling);
			m_queue.push(t);
			wakeOne();
		}
	}

	bool schedule(unsigned int n) const
	{
		// process n elements in wait list
		unsigned int c = 0;
		for (unsigned int i = 0; i < n; i++)
		{
			std::shared_ptr<TaskBase> t;
			if (m_waitList.try_pop(t))
			{
				assert(t.get() != nullptr);
				m_queue.push(t);
				c++; // is awesome
			}
			else
			{
				break;
			}
		}

		if (c > 0)
		{
			if (c >= m_threads.size())
			{
				wakeAll();
			}
			else
			{
				for (unsigned int i = 0; i < c; i++)
					wakeOne();
			}
		}

		return m_waitList.empty();
	}

	template<typename T = void>
	void waitJoin(const std::shared_ptr<Task<T>>& t = std::shared_ptr<Task<T>>()) const
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
#if (_MSC_VER >= 1600)
						if (fut._Is_ready()) // broken std::future_status::deferred temp workaround
							return;
#endif
						break;
					default:
						assert(false);
						break;
					}
				}
			}

			std::shared_ptr<TaskBase> qt;
			while (m_queue.try_pop(qt))
			{
				assert(qt.get() != nullptr);
				(*qt)();
			}
		}
	}

	void sync() 
	{
		// help out flushing on this thread
		while (!schedule(static_cast<unsigned int>(m_waitList.unsafe_size())));

		// and join in.
		waitJoin();
	}

	static unsigned int enqueue(const std::shared_ptr<TaskBase>& t, ConcurrentQueueType<std::shared_ptr<TaskBase>>& queue, TaskStatus status = TsPending)
	{
		unsigned int count = 0;

		for (auto& d : t->getDependencies())
			count += enqueue(d, queue, status);

		if (!t->isContinuation())
		{
			count++;
			t->setStatus(status);
			queue.push(t);
		}

		return count;
	}

	TaskScheduler(const TaskScheduler&);
	TaskScheduler& operator=(const TaskScheduler&);

	// concurrent state
	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
	mutable ConcurrentQueueType<std::shared_ptr<TaskBase>> m_queue;
	mutable ConcurrentQueueType<std::shared_ptr<TaskBase>> m_waitList;
	mutable std::atomic<uint32_t> m_taskConsumerCount;
	mutable std::atomic<bool> m_running;
	mutable std::atomic<bool> m_schedulerTaskEnabled;
	
	// main thread only state
	std::vector<std::shared_ptr<std::thread>> m_threads;
};

// todo: tidy up, generalize and move somewhere
template<unsigned int N>
struct JoinAndSetTupleValueRecursive
{
	template <typename T, typename... Args, unsigned int I=0>
	inline static TaskStatus invoke(T& ret, const Args&... fn)
	{
		return JoinAndSetValueRecursiveImpl<I, (I >= N)>::invoke(ret, fn...);
	}

private:

	template<unsigned int I, bool Terminate>
	struct JoinAndSetValueRecursiveImpl;

	template<unsigned int I>
	struct JoinAndSetValueRecursiveImpl<I, false>
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
					return JoinAndSetValueRecursiveImpl<I+1, (I+1 >= N)>::invoke(ret, fn...);
				case std::future_status::deferred: // broken in VS2012, returns deferred even when running on another thread (not using std::async which VS assumes)
				case std::future_status::timeout:
#if (_MSC_VER >= 1600)
					if (f0._Is_ready()) // broken std::future_status::deferred temp workaround
					{
						std::get<I>(ret) = f0.get();
						return JoinAndSetValueRecursiveImpl<I+1, (I+1 >= N)>::invoke(ret, fn...);
					}
#endif
					return TsScheduledPolling;
				default:
					assert(false);
					break;
				}
			}
			else
			{
				throw std::future_error(std::future_errc::no_state);
			}

			return TsInvalid;
		}
	};

	template<unsigned int I>
	struct JoinAndSetValueRecursiveImpl<I, true>
	{
		template<typename U, typename... X>
		inline static TaskStatus invoke(U&, const X&...)
		{
			return TsDone;
		}
	};
};

} // namespace conc11
