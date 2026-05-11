#pragma once
#include <stdint.h>
#include <cstdlib>
// Macros ////////////////////////////////////////////////////////////////

#define ArraySize(array)        ( sizeof(array)/sizeof((array)[0]) )
#define ASH_INLINE                               inline
#define ASH_FINLINE                              __forceinline
#if defined(ASH_DEBUG)
#define ASH_DEBUG_BREAK                          __debugbreak();
#else
#define ASH_DEBUG_BREAK                          std::abort();
#endif
#define ASH_DISABLE_WARNING(warning_number)      __pragma( warning( disable : warning_number ) )
#define ASH_CONCAT_OPERATOR(x, y)                x##y


#define ASH_STRINGIZE( L )                       #L 
#define ASH_MAKESTRING( L )                      ASH_STRINGIZE( L )
#define ASH_CONCAT(x, y)                         ASH_CONCAT_OPERATOR(x, y)
#define ASH_LINE_STRING                          ASH_MAKESTRING( __LINE__ ) 
#define ASH_FILELINE(MESSAGE)                    __FILE__ "(" ASH_LINE_STRING ") : " MESSAGE
#define ASH_UNIQUE_SUFFIX(PARAM)                 ASH_CONCAT(PARAM, __LINE__ )

template <typename To, typename From>
To safe_cast(From a) {
	To result = (To)a;

	From check = (From)result;
	if (!(check == result))
	{
		ASH_DEBUG_BREAK
	}
	return result;
}

typedef const char* cstring;
