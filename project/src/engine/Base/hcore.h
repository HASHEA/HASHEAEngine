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

#if !defined(ASH_ENABLE_VMA_LEAK_TRACKING)
#if defined(ASH_DEBUG)
#define ASH_ENABLE_VMA_LEAK_TRACKING 1
#else
#define ASH_ENABLE_VMA_LEAK_TRACKING 0
#endif
#endif

#if !defined(ASH_ENABLE_VMA_LEAK_STACKTRACE)
#if defined(ASH_DEBUG)
#define ASH_ENABLE_VMA_LEAK_STACKTRACE 1
#else
#define ASH_ENABLE_VMA_LEAK_STACKTRACE 0
#endif
#endif

#define ASH_VMA_TRACK_CALLSITE __FILE__, static_cast<uint32_t>(__LINE__), __FUNCTION__
#define ASH_VMA_CREATE_BUFFER(ctx, size, usage, memUsage, vkBuffer, allocation, ppData, debugName) \
	(ctx)->vma_create_buffer(size, usage, memUsage, vkBuffer, allocation, ppData, debugName, ASH_VMA_TRACK_CALLSITE)
#define ASH_VMA_DESTROY_BUFFER(ctx, vkBuffer, allocation) \
	(ctx)->vma_destroy_buffer(vkBuffer, allocation, ASH_VMA_TRACK_CALLSITE)
#define ASH_VMA_DESTROY_BUFFER_V(ctx, vkBuffer, allocation) \
	(ctx)->vma_destroy_buffer_v(vkBuffer, allocation, ASH_VMA_TRACK_CALLSITE)
#define ASH_VMA_CREATE_IMAGE(ctx, imageCreateInfo, memUsage, vkImage, allocation, debugName) \
	(ctx)->vma_create_image(imageCreateInfo, memUsage, vkImage, allocation, debugName, ASH_VMA_TRACK_CALLSITE)
#define ASH_VMA_DESTROY_IMAGE(ctx, vkImage, allocation) \
	(ctx)->vma_destroy_image(vkImage, allocation, ASH_VMA_TRACK_CALLSITE)

#define ASH_SAFE_EXECUTE_BEGIN(bResult) bool bResult = true;  bool __bInnerError = false;\
	do { 
	
#define ASH_SAFE_EXECUTE_END(bResult) \
} while(false); \
if (__bInnerError) bResult = false;

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
