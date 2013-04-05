#pragma once

#include "FunctionTraits.h"
#include "TaskUtils.h"
#include "TimeIntervalCollector.h"
#include "Types.h"

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
	TsScheduledOnce,
	TsScheduledPolling,
	TsDone,
	TsInvalid
};

class TaskBase /*abstract*/
{
public:

	virtual void operator()() = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual void setStatus(TaskStatus status) = 0;
	virtual bool isReentrant() const = 0;
	virtual bool isContinuation() const = 0;
	virtual const std::vector<std::shared_ptr<TaskBase>>& getDependencies() const = 0;
	virtual std::shared_ptr<TimeIntervalCollector> getTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) = 0;
	virtual void setTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) = 0;
	
	inline static unsigned int getInstanceCount()
	{
		return s_instanceCount;
	}

protected:

	static unsigned int s_instanceCount;
};

template<typename T>
class Task : public TaskBase, public std::enable_shared_from_this<Task<T>>
{
public:

	typedef T ReturnType;

	Task(const std::string& name = "", bool isReentrant = false, bool isContinuation = false)
		: m_name(name)
		, m_status(TsInvalid)
		, m_isReentrant(isReentrant)
		, m_isContinuation(isContinuation)
		, m_reentrancyFlag(isReentrant)
	{
		s_instanceCount++;
	}

	virtual ~Task()
	{
		assert(m_status != TsPending || m_status != TsScheduledOnce || m_status != TsScheduledPolling);
		s_instanceCount--;
	}

	virtual void operator()() final
	{
		bool expected = m_isReentrant;
		if (m_reentrancyFlag.compare_exchange_strong(expected, true))
		{
			assert(m_function);

			{
				ScopedTimeInterval scope(m_collector);
				m_function();
			}

			assert(m_status != TsInvalid);

			if (m_status == TsDone && !m_continuation.expired())
			{
				if (std::shared_ptr<TaskBase> c = m_continuation.lock())
				{
					(*c)();
				}
				else
				{
					assert(false);
				}
			}
		}
	}

	virtual TaskStatus getStatus() const final
	{
		return m_status;
	}

	virtual void setStatus(TaskStatus status) final
	{
		m_status = status;
	}

	virtual bool isReentrant() const final
	{
		return m_isReentrant;
	}

	virtual bool isContinuation() const final
	{
		return m_isContinuation;
	}

	virtual const std::vector<std::shared_ptr<TaskBase>>& getDependencies() const final
	{
		return m_dependencies;
	}

	virtual std::shared_ptr<TimeIntervalCollector> getTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) final
	{
		return m_collector;
	}

	virtual void setTimeIntervalCollector(std::shared_ptr<TimeIntervalCollector> collector) final
	{
		m_collector = collector;
	}


	inline const std::function<void()>& getFunction() const
	{
		return m_function;
	}

	inline void setFunction(const std::function<void()>& f)
	{
		m_function = f;
	}

	inline void moveFunction(std::function<void()>&& f)
	{
		m_function = std::forward<std::function<void()>>(f);
	}

	inline const std::weak_ptr<TaskBase>& getContinuation() const
	{
		return m_continuation;
	}

	inline void setContinuation(const std::shared_ptr<TaskBase>& c)
	{
		m_continuation = c;
	}

	inline const std::shared_ptr<std::promise<ReturnType>>& getPromise() const
	{
		return m_promise;
	}

	inline void setPromise(const std::shared_ptr<std::promise<ReturnType>>& p)
	{
		m_promise = p;
	}

	inline void movePromise(std::shared_ptr<std::promise<ReturnType>>&& p)
	{
		m_promise = std::forward<std::shared_ptr<std::promise<ReturnType>>>(p);
	}

	inline const std::shared_future<ReturnType>& getFuture() const
	{
		return m_future;
	}

	inline void setFuture(const std::shared_future<ReturnType>& fut)
	{
		m_future = fut;
	}

	inline void moveFuture(std::shared_future<ReturnType>&& fut)
	{
		m_future = std::forward<std::shared_future<ReturnType>>(fut);
	}

	template<typename U>
	inline void addDependencies(const std::vector<std::shared_ptr<U>>& deps)
	{
		m_dependencies.insert(m_dependencies.end(), deps.begin(), deps.end());
	}

	template<typename U, typename... Args>
	inline void addDependencies(const std::shared_ptr<Task<U>>& d0, const std::shared_ptr<Task<Args>>&... dn)
	{
		m_dependencies.push_back(d0);
		addDependencies(dn...);
	}

	inline void addDependencies()
	{
	}

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, const std::string& name = "")
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto p = std::make_shared<std::promise<ThenReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ThenReturnType>>(name, false, true);
		Task<ThenReturnType>& tref = *t;
        auto tf = std::function<void()>([this, &tref, f]
		{
			trySetFuncResult(*tref.getPromise(), f, m_future,
				std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
				std::is_void<typename FunctionTraits<Func>::ReturnType>(),
                std::is_convertible<ReturnType, typename FunctionTraits<Func>::template Arg<0>::Type>());
                //std::is_assignable<ReturnType, typename FunctionTraits<Func>::template Arg<0>::Type>()); // does not compile with clang 4.2

			tref.setStatus(TsDone);
		});
		
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveFunction(std::move(tf));
		t->addDependencies(this->shared_from_this());
		
		m_continuation = t;

		return t;
	}

private:

	Task(const Task&);
	Task& operator=(const Task&);

	std::function<void()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
	std::weak_ptr<TaskBase> m_continuation;
	std::vector<std::shared_ptr<TaskBase>> m_dependencies;
	std::shared_ptr<TimeIntervalCollector> m_collector;
	std::string m_name;
	TaskStatus m_status;
	bool m_isReentrant;
	bool m_isContinuation;
	std::atomic<bool> m_reentrancyFlag;
};

} // namespace conc11
