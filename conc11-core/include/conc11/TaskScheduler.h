#pragma once

#include "FunctionTraits.h"
#include "Task.h"
#include "TaskUtils.h"
#include "TaskTypes.h"
#include "Thread.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <exception>
#include <future>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace conc11
{

class TaskScheduler
{
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
            }), &std::thread::join));
		}
	}

	~TaskScheduler()
	{
		// empty queues
		std::shared_ptr<TaskBase> qt;
		do
        {
			for (auto& q : m_queues)
			{
				while (q.try_pop(qt))
				{
					assert(qt.get() != nullptr);
					(*qt)(*this);
				}
			}
			
			std::this_thread::yield();
			
		} while (m_taskConsumerCount > 0);

		// signal threads to exit
		m_running = false;
		wakeAllThreads();
	}

    inline const std::vector<Thread>& getThreads() const
	{
		return m_threads;
	}
	
	// run task
	template <typename T>
	inline void run(const T& t) const
	{
		assert(t.get() != nullptr);
		(*t)(*this);
	}
	
	// dispatch task chain
	template <typename T>
	inline void dispatch(const T& t) const
	{
		schedule(t);
	}

	// join in on task queue, returning once task t has finished
	void waitJoin(const std::shared_ptr<TaskBase>& t) const
    {
		std::shared_ptr<TaskBase> qt;
		while (true)
        {
			for (auto& q : m_queues)
			{
				while (q.try_pop(qt))
				{
					assert(qt.get() != nullptr);
					(*qt)(*this);
					
					if (t.get() != nullptr && t->getStatus() == TsDone)
						return;
				}
				
				if (t.get() != nullptr && t->getStatus() == TsDone)
					return;
			}
			
			std::this_thread::yield();
		}
	}

private:
	
	void threadMain() const
	{
		while (m_running)
		{
			// process main queue or sleep
            std::shared_ptr<TaskBase> t;
			if (m_queues[TpHigh].try_pop(t))
			{
                assert(t.get() != nullptr);
                (*t)(*this);
			}
			else if (m_queues[TpNormal].try_pop(t))
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

	inline void wakeThreads(unsigned int n) const
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
		
		m_queues[t->getPriority()].push(t);

		wakeThreads(1);
	}
	
	void schedule(const TaskGroup& g) const
	{
		assert(g.size() > 0);
		
		for (auto t : g)
		{
			assert(t.get() != nullptr);
			m_queues[t->getPriority()].push(t);
		}
		
		if (g.size() >= (m_threads.size() - m_taskConsumerCount))
			wakeAllThreads();
		else
			wakeThreads(static_cast<unsigned int>(g.size()));
	}
	
	template <typename T>
	void schedule(const TypedTaskGroup<T>& g) const
	{
		assert(g.size() > 0);
		
		for (auto t : g)
		{
			assert(t.get() != nullptr);
			m_queues[t->getPriority()].push(std::static_pointer_cast<TaskBase>(t));
		}
		
		if (g.size() >= (m_threads.size() - m_taskConsumerCount))
			wakeAllThreads();
		else
			wakeThreads(static_cast<unsigned int>(g.size()));
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

template <typename T>
void Task<T>::operator()(const TaskScheduler& scheduler)
{
	assert(m_function);
	
	{
		assert(g_timeIntervalCollector);
		ScopedTimeInterval scope(*this, *g_timeIntervalCollector);
		
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
