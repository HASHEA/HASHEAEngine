#pragma once

#include "KEnginePub/Public/IKTexture.h"
#include "../IGFX_Private.h"
#include <atomic>

namespace gfx
{
	// 有没有作为地形，绘制块有没有hole的查询表
	struct TerrainHLB
	{
		TerrainHLB(uint8_t* pRawData, uint32_t width, uint32_t height);
		BOOL HasTerrainRegionHole(uint32_t i, uint32_t j);
		BOOL HasTerrainSectionHole(uint32_t i, uint32_t j);

	private:
		uint8_t m_block32[16][16];
		uint8_t m_block128[4][4];
	};

	class KTextureRaw : public KGfxTexture
	{
	public:
		KTextureRaw();
		~KTextureRaw();

		virtual KRESOURCETYPE GetResourceType() override;
		virtual uint64_t      GetResourceSize() override;

		int AddRef() override;
		int Release() override;
		int GetRef() override;

		BOOL     Load() override;
		BOOL     PostLoad() override;
		uint32_t  GetFormat() override;
		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

		TextureType          GetTextureType(void) const;
		uint64_t             GetNameHash() override;

		void        SetResourceName(const char* pFileName);
		KUniqueStr  GetResourceName() override;
		const char* GetName() override;

		gfx::KGfxMemTexture* GetMemTextureVK();
		TerrainHLB*          GetTerrainHLB();
        uint64_t             GetId() override;

        void* GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;
        IKGFX_TextureView* GetSRV() const override;
        const KGFX_TextureDesc& GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

    private:
		std::atomic<int>     m_nRef;
		KUniqueStr           m_ustrName;
		uint64_t             m_nameHash;
		uint8_t*             m_pRawData;
		uint32_t             m_uBytes;
		uint32_t             m_uWidth;
		uint32_t             m_uHeight;
		gfx::KGfxMemTexture* m_pMemTextureVK;

		TerrainHLB* m_pTerrainHLB;
	};
} // namespace gfx
