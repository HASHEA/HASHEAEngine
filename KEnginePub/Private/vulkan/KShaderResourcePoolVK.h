#pragma once
#include "KShaderResourceVK.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class KShaderResourcePoolVK
{
public:
	KShaderResourcePoolVK();
	BOOL Init();
	virtual ~KShaderResourcePoolVK();
	KShaderResourceVK* RequestShaderResource(
		const char* szShaderSource,
		const NSKBase::tagFileLocation& sIncludeShaderLoc,
		const char* szShaderDef,
		const char* szMacro,
		BOOL&       bLoading,
		KEnumMtlTaskLevel uThreadLevel,
        std::map<const_pool_str, gfx::KSamplerState>* mapSamplerState = nullptr
	);
	void RemoveShaderResource(KShaderResourceVK* pShaderResource);

private:
	std::mutex                             m_shaderResourceLock;
	std::unordered_map<uint64_t, KShaderResourceVK*> m_mapShaderResourceVK;
    
	std::unordered_set<std::string>                  m_setErrorData;

#ifdef _WIN32
	FILE* m_pShaderPreBuildLogFile;
#endif
	std::vector<KShaderResourceVK *> m_vecPreLoadShaderResource;
};

namespace NSEngine
{
	extern "C" BOOL                   CreateShaderResourcePoolVK();
	extern "C" BOOL                   InitShaderResourcePoolVK();
	extern "C" BOOL                   DestroyShaderResourcePoolVK();
	extern "C" KShaderResourcePoolVK* GetShaderResroucePoolVK();
} // namespace NSEngine
