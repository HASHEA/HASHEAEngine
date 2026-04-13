#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/RenderPass.h"
#include "Base/ds/harray.hpp"

using namespace AshEngine;

namespace RHI
{
	class DX12RenderPass : public RenderPass
	{
	public:
		DX12RenderPass() = default;
		~DX12RenderPass() = default;

		bool init(const RenderPassCreation& ci);

		AshLoadOption get_color_load_op(uint32_t index) const
		{
			return index < m_colorOperations.size() ? m_colorOperations[index] : ASH_LOAD_DONT_CARE;
		}

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return m_name.c_str(); }
		auto get_color_operations() -> const Array<AshLoadOption>& override { return m_colorOperations; }
		auto get_color_attachment_count() -> uint32_t override { return m_numRenderTargets; }
		auto get_color_attachment_format(uint32_t index) -> AshFormat override { return m_colorFormats[index]; }
		auto get_depth_stencil_operations() -> AshLoadOption override { return m_depthOperation; }
		auto get_depth_stencil_format() -> AshFormat override { return m_depthStencilFormat; }
		auto get_multiview_mask() -> uint32_t override { return m_multiviewMask; }
		auto get_color_attachment_final_state(uint32_t index) -> AshResourceState override { return m_colorFinalStates[index]; }
		auto get_depth_stencil_attachment_final_state() -> AshResourceState override { return m_depthStencilFinalState; }

	private:
		uint32_t m_numRenderTargets = 0;
		AshFormat m_colorFormats[k_max_image_outputs] = {};
		AshResourceState m_colorFinalStates[k_max_image_outputs] = {};
		Array<AshLoadOption> m_colorOperations;
		AshFormat m_depthStencilFormat = ASH_FORMAT_UNDEFINED;
		AshResourceState m_depthStencilFinalState = AshResourceState::Unknown;
		AshLoadOption m_depthOperation = ASH_LOAD_DONT_CARE;
		AshLoadOption m_stencilOperation = ASH_LOAD_DONT_CARE;
		uint32_t m_multiviewMask = 0;
		std::string m_name;
	};
}
