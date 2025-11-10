#pragma once
#include <stdint.h>
#define BIT(x) (1 << x)
#define NO_COPYABLE(TypeName) \
	TypeName(const TypeName &) = delete;   \
	TypeName(TypeName &&) = delete;	\
	TypeName& operator=(TypeName &&) = delete;	\
	TypeName& operator=(const TypeName &) = delete
#ifdef ASH_ENGINE
#define ASH_API __declspec(dllexport)
#else
#define ASH_API __declspec(dllimport)
#endif // ASH_ENGINE

#define NO_COPYABLE(TypeName) \
	TypeName(const TypeName &) = delete;   \
	TypeName(TypeName &&) = delete;	\
	TypeName& operator=(TypeName &&) = delete;	\
	TypeName& operator=(const TypeName &) = delete

		
#define TYPE_TO_STRING(val) #val

#define ASH_SAFE_EXECUTE_BEGIN(bResult) bool bResult = true;  bool __bInnerError = false;\
	do { 
	
#define ASH_SAFE_EXECUTE_END(bResult) \
    if (__bInnerError) bResult = false;\
} while(false);

#define ASH_PROCESS_SUCCESS(condition) if(condition) break;
#define ASH_PROCESS_ERROR(condition) if(!condition) {__bInnerError = true; break;}

