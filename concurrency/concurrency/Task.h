#pragma once

#include "Types.h"

#include <functional>
#include <utility>

namespace conc11
{

class Task : public std::function<void()>
{
	typedef std::function<void()> BaseType;

public:

	enum Priority
	{
		Low = 0,
		Normal,
		High
	};

	Task(Priority priority = Normal)
		: BaseType()
		, m_priority(priority)
	{ }

	Task(const Task& t)
		: BaseType(t)
		, m_priority(t.m_priority)
	{ }

	Task(Task&& t)
		: BaseType(std::forward<BaseType>(t))
		, m_priority(std::forward<Priority>(t.m_priority))
	{ }

	template<class Func>
	Task(Func&& f, Priority priority = Normal)
		: BaseType(std::forward<Func>(f))
		, m_priority(priority)
	{ }

	template<class Func>
	Task(const Func& f, Priority priority = Normal)
		: BaseType(f)
		, m_priority(priority)
	{ }

	Task(std::nullptr_t null, Priority priority = Normal)
		: BaseType(null)
		, m_priority(priority)
	{ }

	template<class Func>
	Task(std::reference_wrapper<Func> f, Priority priority = Normal)
		: BaseType(f)
		, m_priority(priority)
	{ }

	~Task()
	{ }

	Task& operator=(const Task& rhs)
	{
		BaseType::operator=(rhs);
		m_priority = rhs.m_priority;
		return (*this);
	}

	Task& operator=(Task&& rhs)
	{
		BaseType::operator=(std::forward<BaseType>(rhs));
		m_priority = std::forward<Priority>(rhs.m_priority);
		return (*this);
	}

	template<class Func>
	Task& operator=(Func&& f)
	{
		BaseType::operator=(std::forward<Func>(f));
		m_priority = Normal;
		return (*this);
	}

	Task& operator=(std::nullptr_t null)
	{
		BaseType::operator=(null);
		m_priority = Normal;
		return (*this);
	}

	template<class Func>
	Task& operator=(std::reference_wrapper<Func> f)
	{
		BaseType::operator=(f);
		m_priority = Normal;
		return (*this);
	}

	inline void operator()() const
	{
		(*static_cast<const BaseType*>(this))();
		// todo: run continuations here.
	}

	inline void swap(Task& rhs)
	{
		BaseType::swap(rhs);
		std::swap(m_priority, rhs.m_priority);
	}

	inline bool operator<(const Task& rhs) const { return m_priority < rhs.m_priority; }

private:

	Priority m_priority;
};

} // namespace conc11
