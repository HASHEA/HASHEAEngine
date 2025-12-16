#pragma once

#include "KEnginePub/Public/IKTexture.h"
#include "../IGFX_Private.h"
#include <atomic>

namespace gfx
{
	class KTextureMask : public KGfxTexture
	{
	public:
		KTextureMask();
		~KTextureMask();

		virtual KRESOURCETYPE GetResourceType() override;
		uint64_t              GetResourceSize() override;
		virtual BOOL          Load() override;
		virtual BOOL          Create(gfx::enumTextureFormat textureFormat, uint32_t width, uint32_t height, uint8_t* pData, uint32_t ubytes);
		virtual BOOL          LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData) override;
		BOOL                  FillData(uint8_t* pData, uint32_t ubytes);
		BOOL                  UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height, uint8_t* pData, uint32_t ubytes);

		int AddRef() override;
		int Release() override;
		int GetRef() override;

		BOOL     PostLoad() override;
		uint32_t GetFormat() override;
		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

		TextureType GetTextureType(void) const;
		uint64_t    GetNameHash() override;

		void        SetResourceName(const char* pFileName);
		KUniqueStr  GetResourceName() override;
		const char* GetName() override;

		gfx::KGfxMemTexture* GetMemTextureVK();
		uint64_t             GetId() override;

        void* GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;
        IKGFX_TextureView* GetSRV() const override;
        const KGFX_TextureDesc& GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

    private:
		std::atomic<int>       m_nRef;
		KUniqueStr             m_ustrName;
		uint64_t               m_nameHash;
		uint8_t*               m_pRawData;
		uint32_t               m_uWidth;
		uint32_t               m_uHeight;
		gfx::KGfxMemTexture*   m_pMemTextureVK;
		gfx::enumTextureFormat m_textureFormat;
	};
} // namespace gfx
