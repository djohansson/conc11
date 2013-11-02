#pragma once

#include "FunctionTraits.h"
#include "Task.h"
#include "TaskUtils.h"
#include "TaskTypes.h"
#include "Thread.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace conc11
{
	
template<unsigned int N>
struct AddDependencyRecursive;
	
template<unsigned int N>
struct SetTupleValueRecursive;

class TaskScheduler
{
	enum TaskPriority
	{
		TpNormal = 0,
		TpHigh,
		
		TpCount
	};
	
public:

	TaskScheduler(unsigned int threadCount = std::max(2U, std::thread::hardware_concurrency()) - 1)
		: m_taskConsumerCount(threadCount)
		, m_running(true)
		, m_schedulerTaskEnabled(false)
	{
        m_threads.reserve(threadCount);

        for (unsigned int i = 0; i < threadCount; i++)
		{
            m_threads.emplace_back(Thread(std::thread([this]
			{
				try
				{
					threadMain();
				}
				catch (const std::future_error& e)
				{
					(void)e;
					assert(false);
				}
				catch (...)
				{
					assert(false);
				}

				m_taskConsumerCount--;

				std::notify_all_at_thread_exit(m_cv, std::move(std::unique_lock<std::mutex>(m_mutex)));
            }), &std::thread::join));
		}
	}

	~TaskScheduler()
	{
		waitJoin();

		// signal threads to exit
		{
			auto t = std::make_shared<Task<UnitType>>("killThreads");
			Task<UnitType>& tref = *t;
			auto tf = std::function<TaskStatus()>([this, &tref]
			{
				m_running = false;
				
				trySetResult(*tref.getPromise());
				
				return TsDone;
			});
			t->moveFunction(std::move(tf));
			
            m_queues[TpNormal].push(t);
			wakeThreads();
		}

        m_threads.clear(); // = join

		for (auto& q : m_queues)
			assert(q.empty());
	}

    inline const std::vector<Thread>& getThreads() const
	{
		return m_threads;
	}

	// ReturnType Func(...) w/o dependency
	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::string& name = "", const float* color = nullptr) const
	{
		return createTaskWithoutDependency(
			f,
			name,
			color,
			std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
			std::is_void<typename FunctionTraits<Func>::ReturnType>());
	}

	// ReturnType Func(void) with dependencies
	template<typename Func, typename T>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name = "", const float* color = nullptr) const
	{
		return createTaskWithDependency(
			f,
			dependency,
			name,
			color, 
			std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
			std::is_void<typename FunctionTraits<Func>::ReturnType>(),
			std::is_assignable<T, typename FunctionTraits<Func>::template Arg<0>::Type>());
	}
	
	// join heterogeneous tasks without passing any return values
	std::shared_ptr<Task<UnitType>> join(const TaskGroup& g) const
	{
		return joinTasks(g);
	}
	
	/*
	// join homogeneous tasks with passed returned values
	template<typename TaskContainer>
	std::shared_ptr<Task<std::vector<typename TaskContainer::value_type::element_type::ReturnType>>> join(const TaskContainer& c) const
	{
		return joinTasks(c);
	}

	// join heterogeneous tasks with passed returned values
	template<typename T, typename U, typename... Args>
	std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn) const
	{
		return joinTasks(f0, f1, fn...);
	}
	*/
	
	// run task chain
	inline void run(const std::shared_ptr<TaskBase>& t) const
	{		
		assert(t.get() != nullptr);
		(*t)(*this);
	}
	
	template<typename T>
	inline void run(const std::shared_ptr<Task<T>>& t) const
	{
		auto tb = std::static_pointer_cast<TaskBase>(t);
		run(tb);
	}

	// dispatch task chain
	inline void dispatch(const std::shared_ptr<TaskBase>& t) const
	{
		schedule(t);
	}
	
	inline void dispatch(const TaskGroup& tg) const
	{
		schedule(tg);
	}
	
	template<typename T>
	inline void dispatch(const std::shared_ptr<Task<T>>& t) const
	{
		auto tb = std::static_pointer_cast<TaskBase>(t);
		dispatch(tb);
	}

	// join in on task queue, emptying the entire queue or returning once task t has finished
	void waitJoin(const std::shared_ptr<TaskBase>& t = std::shared_ptr<Task<void>>()) const
    {
		do
        {
            std::shared_ptr<TaskBase> qt;
			while (m_queues[TpNormal].try_pop(qt))
			{
                assert(qt.get() != nullptr);
                (*qt)(*this);

                if (t.get() != nullptr && t->getStatus() == TsDone)
					return;
			}

            if (t.get() != nullptr && t->getStatus() == TsDone)
                return;
			
		} while (m_taskConsumerCount > 0);
	}

private:

	template<typename Func, typename T, typename U>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, const std::string& name, const float* color, T argIsVoid, U fIsVoid) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto t = std::make_shared<Task<ReturnType>>(name, color);
		Task<ReturnType>& tref = *t;
		auto tf = std::function<TaskStatus()>([&tref, f, argIsVoid, fIsVoid]
		{
			trySetFuncResult(*tref.getPromise(), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));

		return t;
	}

	template<typename Func, typename T, typename U, typename V, typename X>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name, const float* color, U argIsVoid, V fIsVoid, X argIsAssignable) const
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

		auto t = std::make_shared<Task<ReturnType>>(name, color);
		Task<ReturnType>& tref = *t;
		auto tf = std::function<TaskStatus()>([&tref, f, dependency, argIsVoid, fIsVoid, argIsAssignable]
		{
			trySetFuncResult(*tref.getPromise(), f, dependency->getFuture(), argIsVoid, fIsVoid, argIsAssignable);
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));
		
		t->addDependency();
		dependency->addWaiter(t);

		return t;
	}
	
	std::shared_ptr<Task<UnitType>> joinTasks(const TaskGroup& g) const
	{
		auto t = std::make_shared<Task<UnitType>>("joinTasks");
		Task<UnitType>& tref = *t;
		auto tf = std::function<TaskStatus()>([&tref]
		{
			trySetResult(*tref.getPromise());
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));
		
		for (auto f : g)
		{
			t->addDependency();
			f->addWaiter(t);
		}
		
		return t;
	}
	
	/*
	template<typename TaskContainer>
	std::shared_ptr<Task<std::vector<typename TaskContainer::value_type::element_type::ReturnType>>> joinTasks(const TaskContainer& c) const
	{
		typedef std::vector<typename TaskContainer::value_type::element_type::ReturnType> ReturnType;
		
		auto t = std::make_shared<Task<ReturnType>>("joinTasks");
		Task<ReturnType>& tref = *t;
		auto tf = std::function<TaskStatus()>([&tref, c] // fixme: no shared_ptr:s can go in here
		{
			ReturnType ret;
			ret.reserve(c.size());
			
			for (auto n : c)
				ret.push_back(n->getFuture().get());
			
			trySetResult(*tref.getPromise(), std::move(ret));
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));
		
		for (auto f : c)
		{
			t->addDependency();
			f->addWaiter(t);
		}
		
		return t;
	}

	template<typename T, typename... Args>
	std::shared_ptr<Task<std::tuple<T, Args...>>> joinTasks(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<Args>>&... fn) const
	{
		typedef std::tuple<T, Args...> ReturnType;

		auto t = std::make_shared<Task<ReturnType>>("joinTasks");
		Task<ReturnType>& tref = *t;
		auto tf = std::function<TaskStatus()>([&tref, f0, fn...] // fixme: no shared_ptr:s can go in here
		{
			ReturnType ret;
			SetTupleValueRecursive<(1+sizeof...(Args))>::invoke(ret, f0->getFuture(), fn->getFuture()...);
			trySetResult(*tref.getPromise(), std::move(ret));
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));
		
		AddDependencyRecursive<(1+sizeof...(Args))>::invoke(t, f0, fn...);

		return t;
	}
	*/

	void threadMain() const
	{
		while (m_running)
		{
			// process main queue or sleep
            std::shared_ptr<TaskBase> t;
			if (m_queues[TpNormal].try_pop(t))
			{
                assert(t.get() != nullptr);
                (*t)(*this);
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

	inline void wakeThreads(unsigned int n = 1) const
	{
		assert(n > 0);
		
		std::unique_lock<std::mutex> lock(m_mutex);		
		for (decltype(n) i = 0; i < n; i++)
			m_cv.notify_one();
	}

	inline void wakeAllThreads() const
	{
		std::unique_lock<std::mutex> lock(m_mutex);		
		m_cv.notify_all();
	}

	void schedule(const std::shared_ptr<TaskBase>& t) const
	{
		assert(t.get() != nullptr);
		
		m_queues[TpNormal].push(t);

		wakeThreads();
	}
	
	void schedule(const TaskGroup& tg) const
	{
		assert(tg.size() > 0);
		
		for (auto t : tg)
		{
			assert(t.get() != nullptr);
			m_queues[TpNormal].push(t);
		}
		
		if (tg.size() >= (m_threads.size() - m_taskConsumerCount))
			wakeAllThreads();
		else
			wakeThreads(static_cast<unsigned int>(tg.size()));
	}

	TaskScheduler(const TaskScheduler&);
	TaskScheduler& operator=(const TaskScheduler&);

	// concurrent state
	mutable std::mutex m_mutex;
	mutable std::condition_variable m_cv;
    mutable ConcurrentQueueType<std::shared_ptr<TaskBase>> m_queues[TpCount];
	mutable std::atomic<uint32_t> m_taskConsumerCount;
	mutable std::atomic<bool> m_running;
	mutable std::atomic<bool> m_schedulerTaskEnabled;

	// main thread only state
    std::vector<Thread> m_threads;
};
	
template<unsigned int N>
struct AddDependencyRecursive
{
	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(const T& t, Args&... fn)
	{
		AddDependencyRecursiveImpl<I, (I >= N)>::invoke(t, fn...);
	}
	
private:
	
	template<unsigned int I, bool Terminate>
	struct AddDependencyRecursiveImpl;
	
	template<unsigned int I>
	struct AddDependencyRecursiveImpl<I, false>
	{
		template<typename U, typename V, typename... X>
		static void invoke(const U& t, V& f0, X&... fn)
		{
			t->addDependency();
			f0->addWaiter(t);
			AddDependencyRecursiveImpl<I+1, (I+1 >= N)>::invoke(t, fn...);
		}
	};
	
	template<unsigned int I>
	struct AddDependencyRecursiveImpl<I, true>
	{
		template<typename U, typename... X>
		inline static void invoke(const U&, X&...)
		{
		}
	};
};
	
template<unsigned int N>
struct SetTupleValueRecursive
{
	template <typename T, typename... Args, unsigned int I=0>
	inline static void invoke(T& ret, const Args&... fn)
	{
		SetTupleValueRecursiveImpl<I, (I >= N)>::invoke(ret, fn...);
	}
	
private:
	
	template<unsigned int I, bool Terminate>
	struct SetTupleValueRecursiveImpl;
	
	template<unsigned int I>
	struct SetTupleValueRecursiveImpl<I, false>
	{
		template<typename U, typename V, typename... X>
		static void invoke(U& ret, const V& f0, const X&... fn)
		{
			std::get<I>(ret) = f0.get();
			SetTupleValueRecursiveImpl<I+1, (I+1 >= N)>::invoke(ret, fn...);
		}
	};
	
	template<unsigned int I>
	struct SetTupleValueRecursiveImpl<I, true>
	{
		template<typename U, typename... X>
		inline static void invoke(U&, const X&...)
		{
		}
	};
};

template <typename T>
void Task<T>::operator()(const TaskScheduler& scheduler)
{
	assert(m_function);
	
	{
		ScopedTimeInterval scope(m_name, m_debugColor);
		
		if (getStatus() == TsDone)
		{
			reset();
			
			for (auto t : m_waiters)
				t->addDependency();
		}
		
		setStatus(m_function());
	}
	
	assert(getStatus() == TsDone);
	
	if (m_waiters.size() > 0)
	{
		if (m_waiters[0]->releaseDependency())
			scheduler.run(m_waiters[0]);
		
		for (unsigned int i = 1; i < m_waiters.size(); i++)
			if (m_waiters[i]->releaseDependency())
				scheduler.dispatch(m_waiters[i]);
	}
}

} // namespace conc11
