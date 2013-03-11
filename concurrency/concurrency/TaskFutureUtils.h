#pragma once

#include "Types.h"

#include <future>

namespace conc11
{

// Fut Func(Arg)
template<typename Fut, typename Func, typename Arg>
inline static bool trySetFuncResult(std::promise<Fut>& p, Func f, const std::shared_future<Arg>& arg, std::false_type /*argIsVoid*/, std::false_type /*fIsVoid*/, std::true_type /*argIsAssignable*/)
{
	bool result = true;
	try
	{
		p.set_value(f(arg.get()));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
		result = false;
	}
	return result;
}

// void Func(Arg)
template<typename Func, typename Arg>
inline static bool trySetFuncResult(std::promise<UnitType>& p, Func f, const std::shared_future<Arg>& arg, std::false_type /*argIsVoid*/, std::true_type /*fIsVoid*/, std::true_type /*argIsAssignable*/) 
{
	bool result = true;
	try
	{
		f(arg.get());
		p.set_value(UnitType(0));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
		result = false;
	}
	return result;
}

// Fut Func(void)
template<typename Fut, typename Func>
inline static bool trySetFuncResult(std::promise<Fut>& p, Func f, const std::shared_future<UnitType>& /*arg*/, std::true_type /*argIsVoid*/, std::false_type /*fIsVoid*/, std::false_type /*argIsAssignable*/) 
{
	bool result = true;
	try
	{
		p.set_value(f());
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
		result = false;
	}
	return result;
}

// void Func(void)
template<typename Func>
inline static bool trySetFuncResult(std::promise<UnitType>& p, Func f, const std::shared_future<UnitType>& /*arg*/, std::true_type /*argIsVoid*/, std::true_type /*fIsVoid*/, std::false_type /*argIsAssignable*/) 
{
	bool result = true;
	try
	{
		f();
		p.set_value(UnitType(0));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
		result = false;
	}
	return result;
}

// join - forward
template<typename Fut>
inline static bool trySetResult(std::promise<Fut>& p, Fut&& val) 
{
	bool result = true;
	try
	{
		p.set_value(std::forward<Fut>(val));
	}
	catch (...)
	{
		p.set_exception(std::current_exception());
		result = false;
	}
	return result;
}

}