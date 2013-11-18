#pragma once

#include "Task.h"
#include "TaskTypes.h"
#include "impl/TaskUtilImpl.h"

#include <framework/FunctionTraits.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace conc11
{

// ReturnType Func(...) w/o dependency
template <typename Func>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, std::string&& name = "", Color&& color = Color())
{
	return impl::createTaskWithoutDependency(
		f,
		std::forward<std::string>(name),
		std::forward<Color>(color),
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>());
}

// ReturnType Func(...) with dependencies
template <typename Func, typename T>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency, std::string&& name = "", Color&& color = Color())
{
	return impl::createTaskWithDependency(
		f,
		dependency,
		std::forward<std::string>(name),
		std::forward<Color>(color),
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>(),
		std::is_assignable<T, typename FunctionTraits<Func>::template Arg<0>::Type>());
}

static std::shared_ptr<Task<UnitType>> join(const TaskGroup& g)
{
	auto t = std::make_shared<Task<UnitType>>(TpHigh, std::move(std::string("join").append(g.getName())), g.getColor());
	Task<UnitType>& tref = *t;
	auto tf = std::function<TaskStatus()>([&tref]
	{
		trySetResult(*tref.getPromise());
		
		return TsDone;
	});
	t->moveFunction(std::move(tf));
	
	for (auto f : g)
	{
		t->addDependency();
		f->addWaiter(t);
	}
	
	return t;
}

template <typename T>
static std::shared_ptr<Task<std::vector<T>>> join(const TypedTaskGroup<T>& g)
{
	typedef std::vector<T> ReturnType;
	typedef std::vector<std::shared_future<T>> FutureContainer;
	
	FutureContainer fc;
	fc.reserve(g.size());
	for (auto i : g)
		fc.push_back(i->getFuture());
	
	auto t = std::make_shared<Task<ReturnType>>(TpHigh, std::move(std::string("join").append(g.getName())), g.getColor());
	Task<ReturnType>& tref = *t;
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
	
	return t;
}

/*
template<typename T, typename... Args>
static std::shared_ptr<Task<std::tuple<T, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<Args>>&... fn)
{
	typedef std::tuple<T, Args...> ReturnType;
	typedef std::tuple<std::shared_future<T>, std::shared_future<Args>...> FutureContainer;
	
	FutureContainer fc;
	SetFutureContainer<(1+sizeof...(Args))>::invoke(t, f0, fn...);

	auto t = std::make_shared<Task<ReturnType>>(TpHigh);
	Task<ReturnType>& tref = *t;
	tref.name = std::string("join");
	tref.color = createColor(128, 128, 128, 255);
	auto tf = std::function<TaskStatus()>([&tref, fc]
	{
		ReturnType ret;
		SetTupleValueRecursive<(1+sizeof...(Args))>::invoke(ret, fc);
		trySetResult(*tref.getPromise(), std::move(ret));
		
		return TsDone;
	});
	t->moveFunction(std::move(tf));
	
	AddDependencyRecursive<(1+sizeof...(Args))>::invoke(t, f0, fn...);

	return t;
}
 */

} // namespace conc11
