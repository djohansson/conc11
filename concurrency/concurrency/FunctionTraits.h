#pragma once

#include <tuple>

namespace conc11
{

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
	static const size_t ArgCount = sizeof...(Args);
    
private:

	template <size_t N, bool HasArgs>
	struct ArgImpl;

	template <size_t N>
	struct ArgImpl<N, true>
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};

	template <size_t N>
	struct ArgImpl<N, false>
	{
		typedef void Type;
	};

public:

	template <size_t N>
	struct Arg : ArgImpl<N, (sizeof...(Args) > 0)> { };
};

// const member function pointer
template<typename R, typename C, typename... Args>
struct FunctionTraits<R(C::*)(Args...) const>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	typedef C ClassType;
	static const size_t ArgCount = sizeof...(Args);

private:

	template <size_t N, bool HasArgs>
	struct ArgImpl;
    
    template <size_t N>
	struct ArgImpl<N, true>
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};

	template <size_t N>
	struct ArgImpl<N, false>
	{
		typedef void Type;
	};

public:

	template <size_t N>
	struct Arg : ArgImpl<N, (sizeof...(Args) > 0)> { };
};

// global function pointer
template<typename R, typename... Args>
struct FunctionTraits<R(*)(Args...)>
{
	typedef R Type(Args...);
	typedef R ReturnType;
	static const size_t ArgCount = sizeof...(Args);

private:
    
    template <size_t N, bool HasArgs>
	struct ArgImpl;
    
    template <size_t N>
	struct ArgImpl<N, true>
	{
		typedef typename std::tuple_element<N, std::tuple<Args...>>::type Type;
	};

	template <size_t N>
	struct ArgImpl<N, false>
	{
		typedef void Type;
	};

public:

	template <size_t N>
	struct Arg : ArgImpl<N, (sizeof...(Args) > 0)> { };
};

} // namespace conc11
