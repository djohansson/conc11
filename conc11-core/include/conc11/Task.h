#pragma once

#include "FunctionTraits.h"
#include "TaskUtils.h"
#include "TimeIntervalCollector.h"
#include "TaskTypes.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace conc11
{

enum TaskStatus
{
	TsPending,
	TsDone,
	TsInvalid
};

struct TaskBase /*abstract*/
{
	virtual void operator()() = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual void addDependency() const = 0;
	virtual bool releaseDependency() const = 0;
	virtual void addWaiter(const std::shared_ptr<TaskBase>& waiter) = 0;
};
	
typedef std::vector<std::shared_ptr<TaskBase>> TaskGroup;

template<typename T>
class Task : public TaskBase
{
public:

	typedef T ReturnType;

	Task(const std::string& name = "", const float* color = nullptr)
		: m_name(name)
		, m_status(TsInvalid)
		, m_waitCount(0)
	{
		if (color)
		{
			m_debugColor[0] = color[0];
			m_debugColor[1] = color[1];
			m_debugColor[2] = color[2];
		}
		else
		{
			m_debugColor[0] = 1.0f;
			m_debugColor[1] = 1.0f;
			m_debugColor[2] = 1.0f;
		}
		
		reset();
	}
	
	~Task()
	{
		m_waiters.clear();
	}

	virtual void operator()() final
	{
		assert(m_function);
		
		{
			ScopedTimeInterval scope(m_name, m_debugColor);
		
			if (getStatus() == TsDone)
			{
				reset();
				
				for (auto t : m_waiters)
				{
					t->addDependency();
				}
			}

			setStatus(m_function());
		}
		
		assert(getStatus() == TsDone);
	
		for (auto t : m_waiters)
		{
			if (t->releaseDependency())
				(*t)();
		}
	}

	virtual TaskStatus getStatus() const final
	{
		return m_status.load(std::memory_order_acquire);
	}

	virtual void addDependency() const final
	{
		addWaitCount();
	}

	virtual bool releaseDependency() const final
	{
		return releaseWaitCount();
	}
	
	virtual void addWaiter(const std::shared_ptr<TaskBase>& waiter) final
	{
		m_waiters.push_back(waiter);
	}

	inline const float* getDebugColor() const
	{
		return m_debugColor;
	}

	inline void setDebugColor(const float color[3])
	{
		m_debugColor[0] = color[0];
		m_debugColor[1] = color[1];
		m_debugColor[2] = color[2];
	}

	inline const std::function<TaskStatus()>& getFunction() const
	{
		return m_function;
	}

	inline void setFunction(const std::function<TaskStatus()>& f)
	{
		m_function = f;
	}

	inline void moveFunction(std::function<TaskStatus()>&& f)
	{
		m_function = std::forward<std::function<TaskStatus()>>(f);
	}

	inline const std::shared_ptr<std::promise<ReturnType>>& getPromise() const
	{
		return m_promise;
	}

	inline const std::shared_future<ReturnType>& getFuture() const
	{
		return m_future;
	}

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, const std::string& name = "", const float* color = nullptr)
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto t = std::make_shared<Task<ThenReturnType>>(name, color);
		Task<ThenReturnType>& tref = *t;
		auto tf = std::function<TaskStatus()>([this, &tref, f]
		{
			trySetFuncResult(*tref.getPromise(), f, m_future,
				std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
				std::is_void<typename FunctionTraits<Func>::ReturnType>(),
				std::is_assignable<ReturnType, typename FunctionTraits<Func>::template Arg<0>::Type>());
			
			return TsDone;
		});
		t->moveFunction(std::move(tf));
		
		t->addDependency();
		addWaiter(t);
		
		return t;
	}

private:

	Task(const Task&);
	Task& operator=(const Task&);
	
	inline void setStatus(TaskStatus status) const
	{
		m_status.store(status, std::memory_order_release);
	}
	
	inline void reset()
	{
		m_promise = std::make_shared<std::promise<ReturnType>>();
		m_future = m_promise->get_future().share();
	}

	inline void addWaitCount() const
	{
		m_waitCount++;
	}

	inline bool releaseWaitCount() const
	{
		return (--m_waitCount == 0);
	}

	std::function<TaskStatus()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
	std::vector<std::shared_ptr<TaskBase>> m_waiters;
	std::string m_name;
	float m_debugColor[3];
	mutable std::atomic<TaskStatus> m_status;
	mutable std::atomic<uint32_t> m_waitCount;
};

} // namespace conc11
