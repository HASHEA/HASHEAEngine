#include "DX12RenderPass.h"

namespace RHI
{
	bool DX12RenderPass::init(const RenderPassCreation& ci)
	{
		m_numRenderTargets = ci.num_render_targets;
		m_depthStencilFormat = ci.depth_stencil_format;
		m_depthStencilFinalState = ci.depth_stencil_final_layout;
		m_depthOperation = ci.depth_operation;
		m_stencilOperation = ci.stencil_operation;
		m_multiviewMask = ci.multiview_mask;
		m_name = ci.name ? ci.name : "";

		m_colorOperations.clear();
		for (uint16_t i = 0; i < ci.num_render_targets; ++i)
		{
			m_colorFormats[i] = ci.color_formats[i];
			m_colorFinalStates[i] = ci.color_final_layouts[i];
			m_colorOperations.push_back(ci.color_operations[i]);
		}

		return true;
	}
}
