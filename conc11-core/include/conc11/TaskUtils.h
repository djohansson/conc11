#pragma once

#include "FunctionTraits.h"
#include "Task.h"
#include "TaskTypes.h"

#include <future>
#include <memory>
#include <string>

namespace conc11
{
	
template <typename Func, typename T, typename U>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, const std::string& name, const float* color, T argIsVoid, U fIsVoid)
{
	typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

	auto t = std::make_shared<Task<ReturnType>>(TpNormal, name, color);
	Task<ReturnType>& tref = *t;
	auto tf = std::function<TaskStatus()>([&tref, f, argIsVoid, fIsVoid]
	{
		trySetFuncResult(*tref.getPromise(), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
		
		return TsDone;
	});
	t->moveFunction(std::move(tf));

	return t;
}

template <typename Func, typename T, typename U, typename V, typename X>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name, const float* color, U argIsVoid, V fIsVoid, X argIsAssignable)
{
	typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;

	auto t = std::make_shared<Task<ReturnType>>(TpNormal, name, color);
	Task<ReturnType>& tref = *t;
	auto tf = std::function<TaskStatus()>([&tref, f, dependency, argIsVoid, fIsVoid, argIsAssignable]
	{
		trySetFuncResult(*tref.getPromise(), f, dependency->getFuture(), argIsVoid, fIsVoid, argIsAssignable);
		
		return TsDone;
	});
	t->moveFunction(std::move(tf));
	
	t->addDependency();
	dependency->addWaiter(t);

	return t;
}

static std::shared_ptr<Task<UnitType>> joinTasks(const TaskGroup& g)
{
	auto t = std::make_shared<Task<UnitType>>(TpHigh, "joinTasks");
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
static std::shared_ptr<Task<std::vector<T>>> joinTasks(const TypedTaskGroup<T>& g)
{
	typedef std::vector<T> ReturnType;
	typedef std::vector<std::shared_future<T>> FutureContainer;
	
	FutureContainer fc;
	for (auto i : g)
		fc.push_back(i->getFuture());
	
	auto t = std::make_shared<Task<ReturnType>>(TpHigh, "joinTasks");
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
static std::shared_ptr<Task<std::tuple<T, Args...>>> joinTasks(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<Args>>&... fn)
{
	typedef std::tuple<T, Args...> ReturnType;
	typedef std::tuple<std::shared_future<T>, std::shared_future<Args>...> FutureContainer;
	
	FutureContainer fc;
	SetFutureContainer<(1+sizeof...(Args))>::invoke(t, f0, fn...);

	auto t = std::make_shared<Task<ReturnType>>("joinTasks");
	Task<ReturnType>& tref = *t;
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
	
// ReturnType Func(...) w/o dependency
template <typename Func>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::string& name = "", const float* color = nullptr)
{
	return createTaskWithoutDependency(
		f,
		name,
		color,
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>());
}

// ReturnType Func(...) with dependencies
template <typename Func, typename T>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTask(Func f, const std::shared_ptr<Task<T>>& dependency, const std::string& name = "", const float* color = nullptr)
{
	return createTaskWithDependency(
		f,
		dependency,
		name,
		color, 
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>(),
		std::is_assignable<T, typename FunctionTraits<Func>::template Arg<0>::Type>());
}

// join tasks
template <typename T>
static auto join(const T& g) -> decltype(joinTasks(g))
{
	return joinTasks(g);
}

/*
// join heterogeneous tasks with passed returned values
template<typename T, typename U, typename... Args>
static std::shared_ptr<Task<std::tuple<T, U, Args...>>> join(const std::shared_ptr<Task<T>>& f0, const std::shared_ptr<Task<U>>& f1, const std::shared_ptr<Task<Args>>&... fn)
{
	return joinTasks(f0, f1, fn...);
}
 */


} // namespace conc11
