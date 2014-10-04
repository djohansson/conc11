#pragma once

#include "TaskScheduler.h"
#include "TaskTypes.h"

#include <framework/FunctionTraits.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace conc11
{

// ReturnType Func(...) w/o dependency
template <typename Func>
static auto createTask(Func f, std::string&& name = "", Color&& color = Color())
{
	return TaskScheduler::createTaskWithoutDependency(
		f,
		std::forward<std::string>(name),
		std::forward<Color>(color),
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>());
}

// ReturnType Func(...) with dependencies
template <typename Func, typename T>
static auto createTask(Func f, const std::shared_ptr<Task<T>>& dependency, std::string&& name = "", Color&& color = Color())
{
	return TaskScheduler::createTaskWithDependency(
		f,
		dependency,
		std::forward<std::string>(name),
		std::forward<Color>(color),
		std::is_void<typename FunctionTraits<Func>::template Arg<0>::Type>(),
		std::is_void<typename FunctionTraits<Func>::ReturnType>(),
		std::is_assignable<T, typename FunctionTraits<Func>::template Arg<0>::Type>());
}
    

static auto join(const std::shared_ptr<UntypedTaskGroup>& g)
{
    return TaskScheduler::join(g);
}
    
template <typename T, typename... Args>
static auto join(const std::shared_ptr<TypedTaskGroup<T, Args...>>& g)
{
    return TaskScheduler::join(g);
}


} // namespace conc11
