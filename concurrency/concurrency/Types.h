#pragma once

namespace conc11
{

struct Nil 
{
};

typedef unsigned char UnitType;

template<typename T>
struct VoidToUnitType
{
	typedef T Type;
};

template<>
struct VoidToUnitType<void>
{
	typedef UnitType Type;
};

} // namespace conc11
