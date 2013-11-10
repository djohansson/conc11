#pragma once

#include <framework/TupleElement.h>

#include <tuple>
#include <type_traits>

template<typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> 
{
};

// non-const member function pointer
template<typename R, typename C, typename... Args>
struct FunctionTraits<R(C::*)(Args...)>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	typedef C ClassType;
	static const unsigned int ArgCount = sizeof...(Args);

	template<unsigned int N>
	struct Arg
	{
		typedef std::tuple<Args...> TupleType;
        typedef typename TupleElement<N, TupleType>::type Type;
	};
};

// const member function pointer
template<typename R, typename C, typename... Args>
struct FunctionTraits<R(C::*)(Args...) const>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	typedef C ClassType;
	static const unsigned int ArgCount = sizeof...(Args);

	template<unsigned int N>
	struct Arg
	{
		typedef std::tuple<Args...> TupleType;
        typedef typename TupleElement<N, TupleType>::type Type;
	};
};

// global function pointer
template<typename R, typename... Args>
struct FunctionTraits<R(*)(Args...)>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	static const unsigned int ArgCount = sizeof...(Args);

	template<unsigned int N>
	struct Arg
	{
		typedef std::tuple<Args...> TupleType;
        typedef typename TupleElement<N, TupleType>::type Type;
	};
};
