#pragma once

#include <tuple>

namespace conc11
{

template<typename T>
struct FunctionTraits 
	: FunctionTraits<decltype(&T::operator())> 
{ 
};

// non-const member function pointer
template<typename R, typename C, typename... Args>
struct FunctionTraits<R(C::*)(Args...)>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	typedef C ClassType;
	static const size_t ArgCount = sizeof...(Args);
	template <size_t N>
	struct Arg
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};
};

// const member function pointer
template<typename R, typename C, typename... Args>
struct FunctionTraits<R(C::*)(Args...) const>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	typedef C ClassType;
	static const size_t ArgCount = sizeof...(Args);
	template <size_t N>
	struct Arg
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};
};

// global function pointer
template<typename R, typename... Args>
struct FunctionTraits<R(*)(Args...)>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	static const size_t ArgCount = sizeof...(Args);
	template <size_t N>
	struct Arg
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};
};

} // namespace conc11
