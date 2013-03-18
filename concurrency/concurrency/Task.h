#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "TaskEnabler.h"
#include "Types.h"

#include <bitset>
#include <cassert>
#include <functional>
#include <future>
#include <string>
#include <utility>

namespace conc11
{

enum TaskPriority
{
	Low = 0,
	Normal,
	High
};

enum TaskStatus
{
	Unscheduled,
	ScheduledOnce,
	ScheduledPolling,
	Done,
	Invalid
};

struct ITask
{
	virtual void operator()() = 0;
	virtual operator bool() const = 0;
	virtual TaskStatus getStatus() const = 0;
	virtual void setStatus(TaskStatus status) = 0;
};

template<typename T>
class Task : public ITask, public NonCopyable
{
public:

	typedef T ReturnType;

	Task(const std::string& name = "")
		: m_name(name)
		, m_status(Unscheduled)
	{
		s_instanceCount++;
	}

	~Task()
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

	inline const std::shared_ptr<ITask>& getContinuation() const
	{
		return m_continuation;
	}

	inline void setContinuation(const std::shared_ptr<ITask>& c)
	{
		m_continuation = c;
	}

	inline void moveContinuation(std::shared_ptr<ITask>&& c)
	{
		m_continuation = std::forward<std::shared_ptr<ITask>>(c);
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
		auto tf = std::function<void()>([=, this]
		{
			trySetFuncResult(*p, f, m_future,
				std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
				std::is_void<FunctionTraits<Func>::ReturnType>(),
				std::is_assignable<ReturnType, FunctionTraits<Func>::Arg<0>::Type>());

			t->setStatus(Done);
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
	std::shared_ptr<ITask> m_continuation;
	std::shared_ptr<ITaskEnabler> m_enabler;
	std::string m_name;
	TaskStatus m_status;

	static unsigned int s_instanceCount;
};

template<typename T>
unsigned int Task<T>::s_instanceCount(0);

} // namespace conc11
