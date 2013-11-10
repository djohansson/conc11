#pragma once

namespace conc11
{

namespace impl
{
	
template <typename Func, typename T, typename U>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithoutDependency(Func f, std::string&& name, Color&& color , T argIsVoid, U fIsVoid)
{
	typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;
	
	auto t = std::make_shared<Task<ReturnType>>(TpNormal);
	Task<ReturnType>& tref = *t;
	tref.name = std::forward<std::string>(name);
	tref.color = std::forward<Color>(color);
	auto tf = std::function<TaskStatus()>([&tref, f, argIsVoid, fIsVoid]
	{
		trySetFuncResult(*tref.getPromise(), f, std::shared_future<UnitType>(), argIsVoid, fIsVoid, std::false_type());
		
		return TsDone;
	});
	t->moveFunction(std::move(tf));
	
	return t;
}

template <typename Func, typename T, typename U, typename V, typename X>
static std::shared_ptr<Task<typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type>> createTaskWithDependency(Func f, const std::shared_ptr<Task<T>>& dependency, std::string&& name, Color&& color, U argIsVoid, V fIsVoid, X argIsAssignable)
{
	typedef typename VoidToUnitType<typename FunctionTraits<Func>::ReturnType>::Type ReturnType;
	
	auto t = std::make_shared<Task<ReturnType>>(TpNormal);
	Task<ReturnType>& tref = *t;
	tref.name = std::forward<std::string>(name);
	tref.color = std::forward<Color>(color);
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
	
} // namespace impl

} // namespace conc11
