#pragma once

namespace conc11
{

class NonCopyable
{
protected:
	__forceinline NonCopyable() {}
	__forceinline ~NonCopyable() {}

private:  
	NonCopyable( const NonCopyable& );
	const NonCopyable& operator=(const NonCopyable&);
};

} // namespace conc11
