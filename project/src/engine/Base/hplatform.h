#pragma once
#include <stdint.h>
// Macros ////////////////////////////////////////////////////////////////

#define ArraySize(array)        ( sizeof(array)/sizeof((array)[0]) )
#define HASHEA_INLINE                               inline
#define HASHEA_FINLINE                              __forceinline
#define HASHEA_DEBUG_BREAK                          __debugbreak();
#define HASHEA_DISABLE_WARNING(warning_number)      __pragma( warning( disable : warning_number ) )
#define HASHEA_CONCAT_OPERATOR(x, y)                x##y


#define HASHEA_STRINGIZE( L )                       #L 
#define HASHEA_MAKESTRING( L )                      HASHEA_STRINGIZE( L )
#define HASHEA_CONCAT(x, y)                         HASHEA_CONCAT_OPERATOR(x, y)
#define HASHEA_LINE_STRING                          HASHEA_MAKESTRING( __LINE__ ) 
#define HASHEA_FILELINE(MESSAGE)                    __FILE__ "(" HASHEA_LINE_STRING ") : " MESSAGE
#define HASHEA_UNIQUE_SUFFIX(PARAM)                 HASHEA_CONCAT(PARAM, __LINE__ )
