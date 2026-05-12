#include "Function/Render/SceneDeferredResources.h"

#include "Function/Render/Renderer.h"
#include <algorithm>
#include <limits>
#include <string>

namespace AshEngine
{
	namespace
	{
		static auto make_gbuffer_target_name(size_t index, const GBufferAttachmentDesc& attachment) -> std::string
		{
			std::string name = "Scene";
			if (!attachment.name.empty())
			{
				name += attachment.name;
				return name;
			}

			name += "GBuffer";
			name += static_cast<char>('A' + static_cast<char>(std::min<size_t>(index, 25u)));
			return name;
		}
	}

	bool SceneDeferredResources::ensure(
		Renderer& renderer,
		uint32_t width,
		uint32_t height,
		const GBufferLayoutDesc& layout)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(width > 0);
		ASH_PROCESS_ERROR(height > 0);
		ASH_PROCESS_ERROR(width <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
		ASH_PROCESS_ERROR(height <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
		ASH_PROCESS_ERROR(!layout.attachments.empty());
		ASH_PROCESS_ERROR(layout.layout_hash != 0);

		const bool needs_recreate =
			m_width != width ||
			m_height != height ||
			m_layout_hash != layout.layout_hash ||
			m_gbuffer_targets.size() != layout.attachments.size() ||
			!m_depth_target ||
			!m_lighting_accum_target;
		if (!needs_recreate)
		{
			break;
		}

		reset();
		m_gbuffer_targets.reserve(layout.attachments.size());
		for (size_t attachment_index = 0; attachment_index < layout.attachments.size(); ++attachment_index)
		{
			const GBufferAttachmentDesc& attachment = layout.attachments[attachment_index];
			const std::string target_name = make_gbuffer_target_name(attachment_index, attachment);

			RenderTargetDesc target_desc{};
			target_desc.width = static_cast<uint16_t>(width);
			target_desc.height = static_cast<uint16_t>(height);
			target_desc.format = attachment.format;
			target_desc.shader_resource = true;
			target_desc.unordered_access = false;
			target_desc.name = target_name.c_str();
			target_desc.use_optimized_clear_value = true;
			target_desc.optimized_clear_color = {};

			std::shared_ptr<RenderTarget> target = renderer.create_render_target(target_desc);
			ASH_PROCESS_ERROR(target != nullptr);
			m_gbuffer_targets.push_back(std::move(target));
		}

		RenderTargetDesc depth_desc{};
		depth_desc.width = static_cast<uint16_t>(width);
		depth_desc.height = static_cast<uint16_t>(height);
		depth_desc.format = RenderTextureFormat::D32_SFLOAT;
		depth_desc.shader_resource = true;
		depth_desc.unordered_access = false;
		depth_desc.name = "SceneDeferredDepth";
		depth_desc.use_optimized_clear_value = true;
		depth_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
		m_depth_target = renderer.create_render_target(depth_desc);
		ASH_PROCESS_ERROR(m_depth_target != nullptr);

		RenderTargetDesc lighting_desc{};
		lighting_desc.width = static_cast<uint16_t>(width);
		lighting_desc.height = static_cast<uint16_t>(height);
		lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
		lighting_desc.shader_resource = true;
		lighting_desc.unordered_access = false;
		lighting_desc.name = "SceneDeferredLightingAccum";
		lighting_desc.use_optimized_clear_value = true;
		lighting_desc.optimized_clear_color = {};
		m_lighting_accum_target = renderer.create_render_target(lighting_desc);
		ASH_PROCESS_ERROR(m_lighting_accum_target != nullptr);

		m_width = width;
		m_height = height;
		m_layout_hash = layout.layout_hash;
		m_layout = &layout;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SceneDeferredResources::reset()
	{
		m_width = 0;
		m_height = 0;
		m_layout_hash = 0;
		m_layout = nullptr;
		m_gbuffer_targets.clear();
		m_depth_target.reset();
		m_lighting_accum_target.reset();
	}

	const GBufferLayoutDesc* SceneDeferredResources::get_layout() const
	{
		return m_layout;
	}

	const std::vector<std::shared_ptr<RenderTarget>>& SceneDeferredResources::get_gbuffer_targets() const
	{
		return m_gbuffer_targets;
	}

	const std::shared_ptr<RenderTarget>& SceneDeferredResources::get_depth_target() const
	{
		return m_depth_target;
	}

	const std::shared_ptr<RenderTarget>& SceneDeferredResources::get_lighting_accum_target() const
	{
		return m_lighting_accum_target;
	}
}
