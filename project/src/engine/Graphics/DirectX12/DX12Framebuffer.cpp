#include "DX12Framebuffer.h"
#include "DX12Texture.h"
#include "DX12TextureView.h"
#include "DX12DescriptorHeap.h"
#include "Graphics/RenderPass.h"

namespace RHI
{
	bool DX12Framebuffer::init(const FramebufferCreation& ci, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		m_renderPass = ci.renderPass;
		m_depthStencilAttachment = ci.depthStencilAttachment;
		m_shadingRateAttachment = ci.shadingRateAttachment;
		m_width = ci.width;
		m_height = ci.height;
		m_layers = ci.layers;
		m_name = ci.name ? ci.name : "";
		m_rtvCount = 0;

		m_colorAttachments.clear();
		for (uint32_t i = 0; i < ci.colorAttachments.size(); ++i)
		{
			auto& tex = ci.colorAttachments[i];
			m_colorAttachments.push_back(tex);

			auto* dx12Tex = static_cast<DX12Texture*>(tex.get());
			auto rtv = dx12Tex->get_default_rtv();
			if (rtv)
			{
				auto* dx12View = static_cast<DX12TextureView*>(rtv.get());
				m_rtvHandles[m_rtvCount] = dx12View->get_descriptor_handle().cpuHandle;
				m_rtvCount++;
			}
		}

		if (ci.depthStencilAttachment)
		{
			auto* dx12Tex = static_cast<DX12Texture*>(ci.depthStencilAttachment.get());
			const AshResourceState depthState = ci.renderPass ? ci.renderPass->get_depth_stencil_attachment_final_state() : AshResourceState::DSVWrite;
			const bool readOnlyDepth =
				is_set(depthState, AshResourceState::DSVRead) &&
				!is_set(depthState, AshResourceState::DSVWrite);
			auto dsv = readOnlyDepth ? dx12Tex->get_default_read_only_dsv() : dx12Tex->get_default_rtv(); // DSV is stored in defaultRTV for depth textures
			if (dsv)
			{
				auto* dx12View = static_cast<DX12TextureView*>(dsv.get());
				m_dsvHandle = dx12View->get_descriptor_handle().cpuHandle;
				m_hasDsv = true;
			}
		}

		return true;
	}
}
