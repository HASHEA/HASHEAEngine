#pragma once
#include "KGFX_DX12Header.h"

namespace gfx
{
	class KGFX_TextureViewDX12 : public IKGFX_TextureView//, public KGFX_DelayReleaseObject
	{
    public:
		KGFX_TextureViewDX12(const KGFX_TextureViewDesc& Desc, IKGFX_TextureResource* pTex);

		~KGFX_TextureViewDX12() override;
        KGFX_TextureViewDX12(const KGFX_TextureViewDX12&) = delete;
        KGFX_TextureViewDX12& operator=(const KGFX_TextureViewDX12&) = delete;
        KGFX_TextureViewDX12(KGFX_TextureViewDX12&& other) noexcept;
        KGFX_TextureViewDX12& operator=(KGFX_TextureViewDX12&& other) noexcept;

        IKGFX_TextureResource* GetResource() const override;
	
        uintptr_t GetNativeHandle() override;
        uint32_t GetBindlessHandle() override;
		const KGFX_TextureViewDesc& GetViewDesc() const override;

        void SetDebugName(const char* szDebugName) override;

        int32_t Release() override;

	private:

		D3D12Descriptor GetSRV() const;
		D3D12Descriptor GetUAV() const;
		D3D12Descriptor GetRTV() const;
		D3D12Descriptor GetDSV() const;
        IKGFX_TextureResource* m_pTex = nullptr;
		KGFX_TextureViewDesc m_Desc = {};
		uintptr_t m_uNativeHandle = 0;
        BindlessDescriptor m_BindlessDescriptor = {};
	};
}
