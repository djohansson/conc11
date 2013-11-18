#pragma once

// This header contains workarounds for problems regarding std::tuple_element

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

