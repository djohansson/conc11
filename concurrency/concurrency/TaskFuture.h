#pragma once

#include "FunctionTraits.h"
#include "Types.h"

#include <future>

namespace conc11
{

class TaskScheduler;

template<typename T>
class TaskFuture : public std::shared_future<typename VoidToUnitType<T>::Type>
{
public:

	typedef typename VoidToUnitType<T>::Type ReturnType;

	TaskFuture(const TaskScheduler& scheduler)
		: std::shared_future<ReturnType>()
		, m_scheduler(scheduler)
	{ }

	TaskFuture(const TaskFuture& f)
		: std::shared_future<ReturnType>(f)
		, m_scheduler(f.m_scheduler)
	{ }

	TaskFuture& operator=(const TaskFuture& f)
	{
		std::shared_future<ReturnType>::operator=(f);
		m_scheduler = f.m_scheduler;
		return *this;
	}

	TaskFuture(std::future<ReturnType>&& f, const TaskScheduler& scheduler)
		: std::shared_future<ReturnType>(std::forward<std::future<ReturnType>>(f))
		, m_scheduler(scheduler)
	{ }

	TaskFuture(std::shared_future<ReturnType>&& f, const TaskScheduler& scheduler)
		: std::shared_future<ReturnType>(std::forward<std::shared_future<ReturnType>>(f))
		, m_scheduler(scheduler)
	{ }

	TaskFuture(TaskFuture&& f)
		: std::shared_future<ReturnType>(std::forward<TaskFuture>(f))
		, m_scheduler(std::forward<const TaskScheduler>(f.m_scheduler))
	{ }

	TaskFuture& operator=(TaskFuture&& f)
	{
		std::shared_future<ReturnType>::operator=(std::forward<TaskFuture>(f));
		m_scheduler = std::forward<const TaskScheduler>(f.m_scheduler);
		return *this;
	}

	~TaskFuture()
	{ }

	template<typename Func>
	auto then(Func f) const -> TaskFuture<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>
	{
		return m_scheduler.createTask(f, *this);
	}

	bool ready() const
	{
		return _Is_ready();
	}

private:

	const TaskScheduler& m_scheduler;
};

} // namespace conc11
