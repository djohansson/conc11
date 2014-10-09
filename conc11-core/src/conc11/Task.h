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
#include <tuple>
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
    
class TaskBase
{
    friend class TaskScheduler;
    
public:
    
    virtual ~TaskBase()
    {
        s_instanceCount--;
    }
    
    virtual void operator()(const TaskScheduler& scheduler) = 0;
    virtual void wait() const = 0;
    virtual void reset() = 0;
    
    inline TaskStatus getStatus() const
    {
        return m_status;
    }
    
    inline TaskPriority getPriority() const
    {
        return m_priority;
    }
    
    inline void setPriority(TaskPriority priority) const
    {
        m_priority = priority;
    }
	
	inline const std::string& getName() const { return m_name; }
	inline void setName(std::string&& name) { m_name = std::forward<std::string>(name); }

	inline Color getColor() const { return m_color; }
	inline void setColor(Color&& color) { m_color = std::forward<Color>(color); }
    
    inline TimeInterval& interval() { return m_interval; }
    
    inline void addDependency() const
    {
        addWaitCount();
    }
    
    inline bool releaseDependency() const
    {
        return releaseWaitCount();
    }
    
protected:
    
    struct InitData
    {
        std::string name;
        Color color;
        TaskPriority priority;
    };
    
    explicit TaskBase(InitData&& data)
    : m_color(std::forward<Color>(data.color))
    , m_name(std::forward<std::string>(data.name))
    , m_status(TsUnscheduled)
    , m_priority(std::forward<TaskPriority>(data.priority))
    , m_waitCount(0)
    {
        s_instanceCount++;
    }
    
    inline std::vector<std::shared_ptr<TaskBase>>& waiters() { return m_waiters; }
    
    inline void setStatus(TaskStatus status) const
    {
        m_status = status;
    }
    
private:
    
    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;
    
    inline void addWaitCount() const
    {
        m_waitCount++;
    }
    
    inline bool releaseWaitCount() const
    {
        return (--m_waitCount == 0);
    }
	
	Color m_color;
	std::string m_name;
    TimeInterval m_interval;
    std::vector<std::shared_ptr<TaskBase>> m_waiters;
    mutable std::atomic<TaskStatus> m_status;
    mutable std::atomic<TaskPriority> m_priority;
    mutable std::atomic<uint32_t> m_waitCount;
    static std::atomic<uint32_t> s_instanceCount;
};

template<typename T>
class Task : public TaskBase
{
public:

	typedef T ReturnType;
    
    explicit Task(InitData&& data)
    : TaskBase(std::forward<InitData>(data))
   	{
        reset();
    }
    
    static auto create(TaskPriority&& priority, std::string&& name, Color&& color)
    {
        return std::make_shared<Task<ReturnType>>(InitData{
                                                     std::forward<std::string>(name),
                                                     std::forward<Color>(color),
                                                     std::forward<TaskPriority>(priority)});
    }

	virtual ~Task()
	{ }
	
	virtual void operator()(const TaskScheduler& scheduler) final;
	
	virtual void wait() const final
	{
		assert(getStatus() != TsUnscheduled);
		m_future.wait();
	}

	virtual void reset() final
	{
		m_promise = std::make_shared<std::promise<ReturnType>>();
		m_future = m_promise->get_future().share();

		for (auto w : waiters())
			w->addDependency();
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
	auto then(Func f, std::string&& name = "", Color&& color = Color())
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

        auto t = Task<ThenReturnType>::create(TpHigh, std::forward<std::string>(name), std::forward<Color>(color));
		auto& tref = *t;
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
        waiters().push_back(t);
		
		return t;
	}
    
private:

	std::function<TaskStatus()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
};

class UntypedTaskGroup : public Task<UnitType>, public std::vector<std::shared_ptr<TaskBase>>
{
    typedef std::vector<std::shared_ptr<TaskBase>> TaskContainer;
    struct InitData {};
   
public:
    
    explicit UntypedTaskGroup(InitData&& /*data*/)
    : Task<UnitType>(TaskBase::InitData{"UntypedTaskGroup", createColor(128, 128, 128, 255), TpNormal})
    { }
    
    virtual ~UntypedTaskGroup()
    { }
    
    static auto create()
    {
        return std::make_shared<UntypedTaskGroup>(InitData{});
    }
};

template <typename T, typename... Args>
class TypedTaskGroup : public Task<UnitType>, public std::tuple<std::shared_ptr<Task<T>>, std::shared_ptr<Task<Args>>...>
{
    typedef std::tuple<std::shared_ptr<Task<T>>, std::shared_ptr<Task<Args>>...> TaskContainer;
    struct InitData {};

public:
    
    explicit TypedTaskGroup(InitData&& /*data*/, const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<Args>>&... fn)
    : Task<UnitType>(TaskBase::InitData{"TypedTaskGroup", createColor(128, 128, 128, 255), TpNormal})
    , TaskContainer(f0, fn...)
    { }
    
    virtual ~TypedTaskGroup()
    { }
    
    static auto create(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<Args>>&... fn)
    {
        return std::make_shared<TypedTaskGroup<T, Args...>>(InitData{}, f0, fn...);
    }
};


} // namespace conc11
