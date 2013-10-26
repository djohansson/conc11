#pragma once

// This header contains platform specific workarounds for problems regarding std::tuple_element

namespace conc11
{

#if defined(_MSC_VER)

// VC++ 2012 CTP does not like our variadic implementation of tuple, so we have to default to the std version which still uses macro expansion :(
template<unsigned int I, typename... Args>
struct TupleElement : std::tuple_element<I, Args...> { };

#else // clang has a variadic implementation, but it has errors in it. so we use our own.

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

struct Nil
{
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

} // namespace conc11
