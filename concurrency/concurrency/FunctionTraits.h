#pragma once

#include "Types.h"

#include <tuple>
#include <type_traits>

namespace conc11
{

#if defined(_MSC_VER) // VC++ 2012 CTP does not like the variadic implementation of tuple, defaults to macro expansion

template<unsigned int I, typename... Args>
struct TupleElement : std::tuple_element<I, Args...> { };

#else

template<unsigned int I, typename Tuple>
struct TupleElement;

template<>
struct TupleElement<0, std::tuple<>>
{
	typedef void type;
};

template<typename This, typename... Tail>
struct TupleElement<0, std::tuple<This, Tail...>>
{
	typedef This type;
};

template<unsigned int I, typename... Tail>
struct TupleElement<I, std::tuple<Nil, Tail...>> : public TupleElement<0, std::tuple<Nil, Tail...>>
{
	typedef void type;
};

template<unsigned int I, typename This, typename... Tail>
struct TupleElement<I, std::tuple<This, Tail...>> : public TupleElement<I-1, std::tuple<Tail...>>
{
};

template<unsigned int I, typename Tuple>
struct TupleElement<I, const Tuple> : public TupleElement<I, Tuple>
{
	typedef TupleElement<I, Tuple> basetype;
	typedef typename std::add_const<typename basetype::type>::type type;
};

template<unsigned int I, typename Tuple>
struct TupleElement<I, volatile Tuple> : public TupleElement<I, Tuple>
{
	typedef TupleElement<I, Tuple> basetype;
	typedef typename std::add_volatile<typename basetype::type>::type type;
};

template<unsigned int I, typename Tuple>
struct TupleElement<I, const volatile Tuple> : public TupleElement<I, Tuple>
{
	typedef TupleElement<I, Tuple> basetype;
	typedef typename std::add_cv<typename basetype::type>::type type;
};

#endif

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

} // namespace conc11
