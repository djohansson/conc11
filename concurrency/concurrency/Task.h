#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "TaskEnabler.h"
#include "Types.h"

#include <cassert>
#include <functional>
#include <future>
#include <string>
#include <utility>

namespace conc11
{

enum TaskStatus
{
	Unscheduled,
	ScheduledOnce,
	ScheduledPolling,
	Done,
	Invalid
};

class TaskBase
{
public:

	virtual void operator()() = 0;
	virtual operator bool() const = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual void setStatus(TaskStatus status) = 0;

	inline static unsigned int getInstanceCount() { return s_instanceCount; }

protected:

	static unsigned int s_instanceCount;
};

template<typename T>
class Task : public TaskBase, public NonCopyable
{
public:

	typedef T ReturnType;

	Task(const std::string& name = "")
		: m_name(name)
		, m_status(Unscheduled)
	{
		s_instanceCount++;
	}

	virtual ~Task()
	{
		assert(m_status != ScheduledOnce || m_status != ScheduledPolling);
		s_instanceCount--;
	}

	virtual void operator()()
	{
		assert(m_function);

		m_function();

		assert(m_status != Invalid);
		
		if (m_status == Done && m_continuation && *m_continuation)
			(*m_continuation)();
	}

	virtual operator bool() const
	{
		assert(m_function && m_enabler);

		return *m_enabler;
	}

	virtual TaskStatus getStatus() const
	{
		return m_status;
	}

	virtual void setStatus(TaskStatus status)
	{
		m_status = status;
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

	inline const std::shared_ptr<TaskBase>& getContinuation() const
	{
		return m_continuation;
	}

	inline void setContinuation(const std::shared_ptr<TaskBase>& c)
	{
		m_continuation = c;
	}

	inline void moveContinuation(std::shared_ptr<TaskBase>&& c)
	{
		m_continuation = std::forward<std::shared_ptr<TaskBase>>(c);
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

	inline const std::shared_ptr<ITaskEnabler>& getEnabler() const
	{
		return m_enabler;
	}

	inline void setEnabler(const std::shared_ptr<ITaskEnabler>& e)
	{
		m_enabler = e;
	}

	inline void moveEnabler(std::shared_ptr<ITaskEnabler>&& e)
	{
		m_enabler = std::forward<std::shared_ptr<ITaskEnabler>>(e);
	}

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, const std::string& name = "")
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto p = std::make_shared<std::promise<ThenReturnType>>();
		auto fut = p->get_future().share();
		auto t = std::make_shared<Task<ThenReturnType>>(name);
		std::weak_ptr<Task<ThenReturnType>> tw = t;
		auto tf = std::function<void()>([this, tw, f]
		{
			if (auto t = tw.lock())
			{
				trySetFuncResult(*(t->getPromise()), f, m_future,
					std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
					std::is_void<FunctionTraits<Func>::ReturnType>(),
					std::is_assignable<ReturnType, FunctionTraits<Func>::Arg<0>::Type>());

				t->setStatus(Done);
			}
			else
			{
				assert(false);
			}
		});
		
		t->movePromise(std::move(p));
		t->moveFuture(std::move(fut));
		t->moveFunction(std::move(tf));
		t->setEnabler(m_enabler);

		m_continuation = t;

		return t;
	}

private:

	std::function<void()> m_function;
	std::shared_ptr<std::promise<ReturnType>> m_promise;
	std::shared_future<ReturnType> m_future;
	std::shared_ptr<TaskBase> m_continuation;
	std::shared_ptr<ITaskEnabler> m_enabler;
	std::string m_name;
	TaskStatus m_status;
};

} // namespace conc11
