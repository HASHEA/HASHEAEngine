#ifdef VK_USE_PLATFORM_IOS_MVK
#include "KMVK.h"
#include "KVulkanMVK.h"

void GetMTLTexture(gfx::KRenderTarget* pTarget,  id<MTLTexture> *pMTLTexture)
{
    VkImage pImage = GetVKImageFromRenderTarget(pTarget);
    vkGetMTLTextureMVK(pImage, pMTLTexture);
}

#endif
