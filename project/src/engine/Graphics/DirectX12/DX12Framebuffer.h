#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/Framebuffer.h"
#include "Base/ds/harray.hpp"
#include <memory>

using namespace AshEngine;

namespace RHI
{
	class DX12Texture;
	class DX12DescriptorHeapManager;

	class DX12Framebuffer : public Framebuffer
	{
	public:
		DX12Framebuffer() = default;
		~DX12Framebuffer() = default;

		bool init(const FramebufferCreation& ci, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);

		uint32_t get_rtv_count() const { return m_rtvCount; }
		const D3D12_CPU_DESCRIPTOR_HANDLE* get_rtv_handles() const { return m_rtvHandles; }
		bool has_dsv() const { return m_hasDsv; }
		const D3D12_CPU_DESCRIPTOR_HANDLE* get_dsv_handle() const { return m_hasDsv ? &m_dsvHandle : nullptr; }

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return m_name.c_str(); }
		auto get_render_pass() -> std::shared_ptr<RenderPass> override { return m_renderPass; }
		auto get_render_targets() -> Array<std::shared_ptr<Texture>>& override { return m_colorAttachments; }
		auto get_depth_stencil() -> std::shared_ptr<Texture> override { return m_depthStencilAttachment; }
		auto get_shading_rate_attachment() -> std::shared_ptr<Texture> override { return m_shadingRateAttachment; }
		auto get_render_target_clear_color(uint32_t index) -> const AshColorValue& override { return m_clearColors[index]; }
		auto get_depth_stencil_clear_color() -> const AshDepthStencilValue& override { return m_depthStencilClear; }
		auto clear_render_target(uint32_t index, const AshColorValue& color) -> void override { m_clearColors[index] = color; }
		auto clear_depth_stencil(const AshDepthStencilValue& color) -> void override { m_depthStencilClear = color; }
		auto get_width() -> uint32_t override { return m_width; }
		auto get_height() -> uint32_t override { return m_height; }
		auto get_layer_count() -> uint32_t override { return m_layers; }

	private:
		std::shared_ptr<RenderPass> m_renderPass;
		Array<std::shared_ptr<Texture>> m_colorAttachments;
		std::shared_ptr<Texture> m_depthStencilAttachment;
		std::shared_ptr<Texture> m_shadingRateAttachment;

		D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[k_max_image_outputs] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle = {};
		uint32_t m_rtvCount = 0;
		bool m_hasDsv = false;

		AshColorValue m_clearColors[k_max_image_outputs];
		AshDepthStencilValue m_depthStencilClear = { 1.0f, 0 };

		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint32_t m_layers = 0;
		std::string m_name;
	};
}
