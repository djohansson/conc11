#pragma once

#include <framework/FunctionTraits.h>

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
	
class TaskScheduler;

enum TaskStatus
{
	TsUnscheduled,
	TsPending,
	TsDone,
	
	TsCount
};
	
enum TaskPriority
{
	TpNormal = 0,
	TpHigh,
	
	TpCount
};

struct TaskBase /*abstract*/
{
	virtual void operator()(const TaskScheduler& scheduler) = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual TaskPriority getPriority() const = 0;
	virtual void setPriority(TaskPriority p) const = 0;
	virtual void setStatus(TaskStatus s) const = 0;
	virtual void wait() const = 0;
	virtual void reset() = 0;
	
	// todo: hide these
	virtual void addDependency() const = 0;
	virtual bool releaseDependency() const = 0;
	virtual void addWaiter(const std::shared_ptr<TaskBase>& waiter) = 0;
};

template<typename T>
class Task : public TaskBase
{
public:

	typedef T ReturnType;

	Task(TaskPriority&& priority, std::string&& name, Color&& color)
		: m_color(std::forward<Color>(color))
		, m_name(std::forward<std::string>(name))
		, m_status(TsUnscheduled)
		, m_priority(std::forward<TaskPriority>(priority))
		, m_waitCount(0)
	{
		reset();
	}

	virtual ~Task()
	{ }
	
	virtual void operator()(const TaskScheduler& scheduler) final;
	
	virtual TaskStatus getStatus() const final
	{
		return m_status;
	}
	
	virtual TaskPriority getPriority() const final
	{
		return m_priority;
	}
	
	virtual void wait() const final
	{
		assert(m_status != TsUnscheduled);
		m_future.wait();
	}

	virtual void reset() final
	{
		m_promise = std::make_shared<std::promise<ReturnType>>();
		m_future = m_promise->get_future().share();

		for (auto w : m_waiters)
			w->addDependency();
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

	inline const std::string& getName() const { return m_name; }
	inline void setName(std::string&& name) { m_name = std::forward<std::string>(name); }

	inline Color getColor() const { return m_color; }
	inline void setColor(Color&& color) { m_color = std::forward<Color>(color); }

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, std::string&& name = "", Color&& color = Color())
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto t = std::make_shared<Task<ThenReturnType>>(TpHigh, std::forward<std::string>(name), std::forward<Color>(color));
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
	
	virtual void setStatus(TaskStatus status) const final
	{
		m_status = status;
	}
	
	virtual void setPriority(TaskPriority priority) const final
	{
		m_priority = priority;
	}
	
	inline void addWaitCount() const
	{
		m_waitCount++;
	}

	inline bool releaseWaitCount() const
	{
		return (--m_waitCount == 0);
	}

	TimeInterval m_interval;
	Color m_color;
	std::string m_name;
	std::function<TaskStatus()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
	std::vector<std::shared_ptr<TaskBase>> m_waiters;
	mutable std::atomic<TaskStatus> m_status;
	mutable std::atomic<TaskPriority> m_priority;
	mutable std::atomic<uint32_t> m_waitCount;
};
	
class TaskGroupBase
{
public:
		
	TaskGroupBase(std::string&& name, Color&& color)
	: m_color(std::forward<Color>(color))
	, m_name(std::forward<std::string>(name))
	{ }
	
	inline const std::string& getName() const { return m_name; }
	inline void setName(std::string&& name) { m_name = std::forward<std::string>(name); }

	inline Color getColor() const { return m_color; }
	inline void setColor(Color&& color) { m_color = std::forward<Color>(color); }

protected:
	
	Color m_color;
	std::string m_name;
};

class TaskGroup : public TaskGroupBase, public std::vector<std::shared_ptr<TaskBase>>
{
public:
	
	TaskGroup()
	: TaskGroupBase("TaskGroup", createColor(128, 128, 128, 255))
	{ }
};

template <typename T>
class TypedTaskGroup : public TaskGroupBase, public std::vector<std::shared_ptr<Task<T>>>
{
public:
	
	TypedTaskGroup()
	: TaskGroupBase("TypedTaskGroup", createColor(128, 128, 128, 255))
	{ }
};


} // namespace conc11