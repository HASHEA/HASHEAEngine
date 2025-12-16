#include "KEnginePub/Public/IKShaderVK.h"
#include <mutex>
#include "robin_hood.h"
//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"


    // 自定义哈希函数：为string_view实现无分配哈希计算
struct _StringViewHash {
    // FNV1A哈希算法（高效且适合字符串）
    size_t operator()(std::string_view sv) const noexcept {
        constexpr size_t fnv_offset = 14695981039346656037ULL;
        constexpr size_t fnv_prime = 1099511628211ULL;

        size_t hash = fnv_offset;
        for (char c : sv) {
            hash ^= static_cast<size_t>(c);
            hash *= fnv_prime;
        }
        return hash;
    }
};

// 自定义相等性比较器
struct _StringViewEqual {
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b; // 直接比较string_view内容
    }
};


robin_hood::unordered_set<std::string_view,_StringViewHash,_StringViewEqual> g_paramNamePool;
std::list<std::string> g_paramListPool;

//std::unordered_set<std::string> g_paramNamePool;
std::mutex            g_mutexParamNamePool;

const_pool_str GetParamNameByPool(const char* pName)
{
	PROF_CPU_DEEP();
	std::lock_guard<std::mutex> lock(g_mutexParamNamePool);
	const_pool_str              inPoolName = nullptr;
    if(pName)
    {
        //用了string_view代理，就不会每次查找都去创建一个string产生内存分配了，这样效率会提高很多
	    auto                        it         = g_paramNamePool.find(pName);
	    if (it == g_paramNamePool.end())
	    {
            //string的本体放这里
            g_paramListPool.push_back(pName);            
            std::string_view sv = g_paramListPool.back();
            //代理放这里
		    auto ret = g_paramNamePool.emplace(sv);
		    auto i   = ret.first;
		    inPoolName  = i->data();
	    }
	    else
	    {
		    inPoolName = it->data();
	    }
    }
	return inPoolName;
}
