#pragma once

#include "FunctionTraits.h"
#include "NonCopyable.h"
#include "Types.h"

#include <assert.h>

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

struct ITask
{
	virtual void operator()() = 0;
	virtual operator bool() const = 0;
	virtual void enable() = 0;
};

template<typename T>
class Task : public ITask, public NonCopyable
{
public:

	typedef T ReturnType;

	template<typename Func>
	Task(Func&& f, std::future<ReturnType>&& fut, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(std::forward<Func>(f))
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(std::make_shared<bool>(false))
	{
		assert(m_function);
	}

	template<typename Func>
	Task(Func&& f, std::future<ReturnType>&& fut, const std::shared_ptr<bool>& enabler, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(std::forward<Func>(f))
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(enabler)
	{
		assert(m_function);
	}

	template<typename Func>
	Task(const Func& f, std::future<ReturnType>&& fut, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(f)
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(std::make_shared<bool>(false))
	{
		assert(m_function);
	}

	template<typename Func>
	Task(const Func& f, std::future<ReturnType>&& fut, const std::shared_ptr<bool>& enabler, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(f)
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(enabler)
	{
		assert(m_function);
	}

	template<typename Func>
	Task(std::reference_wrapper<Func> f, std::future<ReturnType>&& fut, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(f)
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(std::make_shared<bool>(false))
	{
		assert(m_function);
	}

	template<typename Func>
	Task(std::reference_wrapper<Func> f, std::future<ReturnType>&& fut, const std::shared_ptr<bool>& enabler, TaskPriority priority = Normal, const std::string& name = "")
		: m_function(f)
		, m_future(std::forward<std::future<ReturnType>>(fut))
		, m_priority(priority)
		, m_name(name)
		, m_enabled(enabler)
	{
		assert(m_function);
	}

	virtual ~Task()
	{ }

	virtual void operator()()
	{
		assert(m_enabled && m_function);

		m_function();
		
		if (m_continuation && *m_continuation)
			(*m_continuation)();
	}

	virtual operator bool() const
	{
		assert(m_function);

		return *m_enabled;
	}

	virtual void enable() { *m_enabled = true; }

	inline const std::shared_ptr<bool>& getEnabler() const { return m_enabled; }

	inline std::shared_future<ReturnType> getFuture() const { return m_future; }

	template<typename Func>
	std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> then(Func f, const std::string& name = "")
	{
		typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ThenReturnType;

		auto p = std::make_shared<std::promise<ThenReturnType>>();
		m_continuation = std::make_shared<Task<ThenReturnType>>([=, this]
		{
			trySetFuncResult(*p, f, m_future,
				std::is_void<FunctionTraits<Func>::Arg<0>::Type>(),
				std::is_void<FunctionTraits<Func>::ReturnType>(),
				std::is_assignable<ReturnType, FunctionTraits<Func>::Arg<0>::Type>());
		}, std::move(p->get_future()), m_enabled, m_priority, name);

		return std::static_pointer_cast<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>>(m_continuation);
	}

private:

	std::function<void()> m_function;
	std::shared_future<ReturnType> m_future;
	TaskPriority m_priority;
	std::shared_ptr<ITask> m_continuation;
	std::string m_name;
	std::shared_ptr<bool> m_enabled;
};

} // namespace conc11
