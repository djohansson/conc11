#pragma once

#ifdef CONC11_CORE_EXPORTS
#define CONC11_CORE_API __declspec(dllexport)
#else
#define CONC11_CORE_API __declspec(dllimport)
#endif
