#pragma once

#include <stdint.h>

template <unsigned...>
struct Sum;

template <unsigned size>
struct Sum<size>
{
	enum { value = size };
};

template <unsigned size, unsigned... sizes>
struct Sum<size, sizes...>
{
	enum { value = size + Sum<sizes...>::value };
};

template <unsigned bits>
struct Store;

template <> struct Store<8> { typedef uint8_t Type; };
template <> struct Store<16> { typedef uint16_t Type; };
template <> struct Store<32> { typedef uint32_t Type; };
template <> struct Store<64> { typedef uint64_t Type; };

template <unsigned... sizes>
class Bitfields
{
public:
	
	Bitfields()
	: m_store(0u)
	{ }
	
	typedef typename Store<Sum<sizes...>::value>::Type StoreType;
	
	template <unsigned pos, unsigned b4, unsigned size, unsigned... more>
	friend unsigned getImpl(Bitfields<size, more...> bf);
	
	template <unsigned pos, unsigned b4, unsigned size, unsigned... more>
	friend void setImpl(Bitfields<size, more...>& bf, unsigned val);

	inline operator StoreType() const { return m_store; }
	
private:
	
	StoreType m_store;
};
	
template <unsigned pos, unsigned b4, unsigned size, unsigned... sizes>
unsigned getImpl(Bitfields<size, sizes...> bf)
{
	static_assert(pos <= sizeof...(sizes), "Invalid bitfield access");
	
	if (pos == 0)
	{
		if (size == 1)
			return (bf.m_store & (1u << b4)) != 0;
		
		return (bf.m_store >> b4) & ((1u << size) - 1);
	}
	
	return getImpl<(pos - (pos ? 1 : 0)), (b4 + (pos ? size : 0))>(bf);
}
	
template <unsigned pos, unsigned b4, unsigned size, unsigned... sizes>
void setImpl(Bitfields<size, sizes...>& bf, unsigned val)
{
	static_assert(pos <= sizeof...(sizes), "Invalid bitfield access");
	
	if (pos == 0)
	{
		bf.m_store = (bf.m_store & ~(((1u << size) - 1) << b4)) | (val << b4);
		
		return;
	}
	
	setImpl<(pos - (pos ? 1 : 0)), (b4 + (pos ? size : 0))>(bf, val);
}
	
template <unsigned pos, unsigned... sizes>
unsigned get(Bitfields<sizes...> bf)
{
	return getImpl<pos, 0>(bf);
}
	
template <unsigned pos, unsigned... sizes>
void set(Bitfields<sizes...>& bf, unsigned val)
{
	setImpl<pos, 0>(bf, val);
}

typedef Bitfields<8, 8, 8, 8> Color;
enum ColorComponent
{
	CcRed = 0,
	CcGreen = 1,
	CcBlue = 2,
	CcAlpha = 3,

	CcCount
};

inline static Color createColor(unsigned red, unsigned green, unsigned blue, unsigned alpha)
{
	Color c;
	set<CcRed>(c, red);
	set<CcGreen>(c, green);
	set<CcBlue>(c, blue);
	set<CcAlpha>(c, alpha);
	return c;
}
