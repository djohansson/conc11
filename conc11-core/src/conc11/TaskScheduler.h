#pragma once

#include <framework/FunctionTraits.h>
#include <framework/Thread.h>

#include "Task.h"
#include "TaskTypes.h"

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
        
        assert(TaskBase::s_instanceCount == 0);
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

		if (t->getStatus() == TsDone)
			t->reset();

		(*t)(*this);
	}
	
	// dispatch task chain
	template <typename T>
	inline void dispatch(const T& t) const
	{
		schedule(t);
	}

	// join in on task queue, returning once task t has finished
	void processQueueUntil(const std::shared_ptr<TaskBase>& t) const
    {
        assert(t.get() != nullptr);
        
        std::shared_ptr<TaskBase> qt;
        while (t->getStatus() != TsDone)
        {
            for (auto& q : m_queues)
            {
                while (q.try_pop(qt))
                {
                    assert(qt.get() != nullptr);
                    (*qt)(*this);
                    
                    if (t->getStatus() == TsDone)
                        return;
                }
            }
            
            std::this_thread::yield();
        }
	}

	inline const std::shared_ptr<TimeIntervalCollector>& getTimeIntervalCollector() const { return m_collector; }
	inline void setTimeIntervalCollector(const std::shared_ptr<TimeIntervalCollector>& collector) { m_collector = collector; }
    
    static auto join(const std::shared_ptr<UntypedTaskGroup>& g)
    {
        auto& gref = *g;
        auto gf = std::function<TaskStatus()>([&gref]
                                              {
                                                  gref.clear();
                                                  return TsDone;
                                              });
        g->moveFunction(std::move(gf));
        
        for (auto t : *g)
        {
            g->addDependency();
            t->waiters().push_back(g);
        }
        
        return g;
    }
    
    /*
    template <typename T, typename... Args>
    static auto join(const TypedTaskGroup<T, Args...>& g)
    {
        typedef std::vector<T> ReturnType;
        typedef std::vector<std::shared_future<T>> FutureContainer;
        
        FutureContainer fc;
        fc.reserve(g.size());
        for (auto i : g)
            fc.push_back(i->getFuture());
        
        auto t = std::make_shared<Task<ReturnType>>(TpHigh, std::move(std::string("join").append(g.getName())), g.getColor());
        auto& tref = *t;
        auto tf = std::function<TaskStatus()>([&tref, fc]
                                              {
                                                  ReturnType ret;
                                                  ret.reserve(fc.size());
                                                  for (auto f : fc)
                                                      ret.push_back(f.get());
                                                  
                                                  trySetResult(*tref.getPromise(), std::move(ret));
                                                  
                                                  return TsDone;
                                              });
        t->moveFunction(std::move(tf));
        
        for (auto i : g)
        {
            t->addDependency();
            i->addWaiter(t);
        }
        
        return g;
    }
    
    template<typename T, typename... Args>
    static auto join(const std::shared_ptr<TypedTaskGroup<T, Args...>>& g)
    {
        AddDependencyRecursive<(1+sizeof...(Args))>::invoke(*g);
        
        return g;
    }
     */
    
    template <typename Func, typename T, typename U>
    static auto createTaskWithoutDependency(Func f, std::string&& name, Color&& color , T argIsVoid, U fIsVoid)
    {
        typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;
        
        auto t = Task<ReturnType>::create(TpNormal, std::forward<std::string>(name), std::forward<Color>(color));
        auto& tref = *t;
        auto tf = std::function<TaskStatus()>([&tref, f, argIsVoid, fIsVoid]
                                              {
                                                  trySetFuncResult(*tref.getPromise(), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
                                                  
                                                  return TsDone;
                                              });
        t->moveFunction(std::move(tf));
        
        return t;
    }
    
    template <typename Func, typename T, typename U, typename V, typename X>
    static auto createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, std::string&& name, Color&& color, U argIsVoid, V fIsVoid, X argIsAssignable)
    {
        typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;
        
        auto t = Task<ReturnType>::create(TpNormal, std::forward<std::string>(name), std::forward<Color>(color));
        auto& tref = *t;
        auto depFuture = dependency->getFuture();
        auto tf = std::function<TaskStatus()>([&tref, f, depFuture, argIsVoid, fIsVoid, argIsAssignable]
                                              {
                                                  trySetFuncResult(*tref.getPromise(), f, depFuture, argIsVoid, fIsVoid, argIsAssignable);
                                                  
                                                  return TsDone;
                                              });
        t->moveFunction(std::move(tf));
        
        t->addDependency();
        dependency->waiters().push_back(t);

        return t;
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
		
		if (t->getStatus() == TsDone)
			t->reset();

		t->setStatus(TsPending);

		m_queues[t->getPriority()].push(t);

		wakeThreads(1);
	}
	
	void schedule(const UntypedTaskGroup& g) const
	{
		assert(g.size() > 0);
		
		for (auto t : g)
		{
			assert(t.get() != nullptr);
			
			if (t->getStatus() == TsDone)
				t->reset();

			t->setStatus(TsPending);
			
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
			
			if (t->getStatus() == TsDone)
				t->reset();

			t->setStatus(TsPending);
			
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

	// profiling
	std::shared_ptr<TimeIntervalCollector> m_collector;
};

template <typename T>
void Task<T>::operator()(const TaskScheduler& scheduler)
{
    assert(m_function != nullptr);
    
	//if (m_function)
	{
		ScopedTimeInterval scope(interval(), getColor(), scheduler.getTimeIntervalCollector());
		setStatus(m_function());
	}
    //else
    //{
    //  setStatus(TsDone);
    //}
	
	assert(getStatus() == TsDone);
	
	if (waiters().size() > 0)
	{
		for (unsigned int i = 1; i < waiters().size(); i++)
			if (waiters()[i]->releaseDependency())
				scheduler.dispatch(waiters()[i]);
		
		if (waiters()[0]->releaseDependency())
			scheduler.run(waiters()[0]);
	}
}

} // namespace conc11
