#pragma once
#include <stdint.h>
#include <type_traits>
#include <string_view>
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

namespace ASH_HASH
{
	template <class T, class Hasher = std::hash<T>>
	inline void hash_combine(std::size_t& seed, const T& v, Hasher hasher = Hasher{}) {
		seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
	struct CStringHash {
		std::size_t operator()(const char* str) const {
			if (!str) return 0;
			return std::hash<std::string_view>{}(std::string_view(str));
		}
	};
}

