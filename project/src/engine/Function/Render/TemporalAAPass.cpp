#include "Function/Render/TemporalAAPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_resolve_shader_path = "project/src/engine/Shaders/Deferred/TemporalAAResolve.hlsl";
		static constexpr const char* k_common_shader_path = "project/src/engine/Shaders/Deferred/TemporalAACommon.hlsli";
		static constexpr RenderColorValue k_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct TemporalAARootConstants
		{
			glm::vec4 config0{ 1.0f, 1.0f, 0.9f, 1.0f };
			glm::vec4 config1{ 0.0f, 0.0f, 0.0f, 0.0f };
			glm::vec4 config2{ 0.0f, 0.0f, 1.0f, 0.0f };
		};

		static_assert(sizeof(TemporalAARootConstants) <= 224u);
		static_assert(sizeof(TemporalAARootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto to_graph_dimension(uint32_t value) -> uint16_t
		{
			return static_cast<uint16_t>(std::clamp<uint32_t>(value, 1u, UINT16_MAX));
		}

		auto build_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_resolve_shader_path);
			RHI::hash_shader_file_signature(hash_value, k_common_shader_path);
			return hash_value;
		}

		auto make_compute_desc() -> ComputeProgramDesc
		{
			ComputeProgramDesc desc{};
			desc.shader_path = k_resolve_shader_path;
			desc.compute_entry = "CSMain";
			desc.source_hash = build_source_hash();
			desc.name = "SceneTemporalAAResolve";
			return desc;
		}

		auto make_color_texture_desc(uint32_t width, uint32_t height) -> RenderGraphTextureDesc
		{
			RenderGraphTextureDesc desc{};
			desc.width = to_graph_dimension(width);
			desc.height = to_graph_dimension(height);
			desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			desc.shader_resource = true;
			desc.unordered_access = true;
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = k_clear_color;
			return desc;
		}

		auto make_root_constants(
			const VisibleRenderFrame& frame,
			uint32_t output_width,
			uint32_t output_height,
			const TemporalAAConfig& config,
			bool history_valid) -> TemporalAARootConstants
		{
			// NDC jitter -> UV jitter. NDC->UV uses float2(0.5, -0.5) (matches
			// AshClipToUv); the motion vector decoupling in the shader subtracts
			// the resulting UV-space jitter delta.
			const glm::vec2 jitter_uv = frame.taa_jitter_ndc * glm::vec2(0.5f, -0.5f);
			const glm::vec2 prev_jitter_uv = frame.taa_previous_jitter_ndc * glm::vec2(0.5f, -0.5f);

			TemporalAARootConstants constants{};
			constants.config0 = glm::vec4(
				static_cast<float>(output_width),
				static_cast<float>(output_height),
				config.history_blend,
				config.variance_gamma);
			constants.config1 = glm::vec4(jitter_uv.x, jitter_uv.y, prev_jitter_uv.x, prev_jitter_uv.y);
			constants.config2 = glm::vec4(
				frame.reverse_z ? 1.0f : 0.0f,
				history_valid ? 1.0f : 0.0f,
				config.luminance_weighting ? 1.0f : 0.0f,
				static_cast<float>(static_cast<uint8_t>(config.debug_view)));
			return constants;
		}
	}

	bool TemporalAAPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("TemporalAAPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void TemporalAAPass::shutdown()
	{
		m_resolve_program.reset();
		clear_history();
		m_linear_clamp_sampler.reset();
		m_point_clamp_sampler.reset();
		m_logged_runtime_state = false;
		m_renderer = nullptr;
	}

	void TemporalAAPass::clear_history()
	{
		m_history_entries.clear();
	}

	bool TemporalAAPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		RenderSamplerDesc point_desc{};
		point_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		point_desc.min_filter = RenderSamplerFilter::Nearest;
		point_desc.mag_filter = RenderSamplerFilter::Nearest;
		point_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(point_desc, "SceneTemporalAAPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		RenderSamplerDesc linear_desc = point_desc;
		linear_desc.min_filter = RenderSamplerFilter::Linear;
		linear_desc.mag_filter = RenderSamplerFilter::Linear;
		linear_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(linear_desc, "SceneTemporalAALinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool TemporalAAPass::create_programs(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_resolve_program = renderer.create_compute_program(make_compute_desc());
		ASH_PROCESS_ERROR(m_resolve_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool TemporalAAPass::ensure_history_entry(
		uint64_t view_key,
		uint32_t width,
		uint32_t height,
		TemporalAAHistoryEntry*& out_entry)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_entry = nullptr;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		const uint32_t clamped_width = std::max<uint32_t>(width, 1u);
		const uint32_t clamped_height = std::max<uint32_t>(height, 1u);
		TemporalAAHistoryEntry& entry = m_history_entries[view_key];
		const bool size_changed = entry.width != clamped_width || entry.height != clamped_height;
		if (size_changed)
		{
			entry = {};
			entry.width = clamped_width;
			entry.height = clamped_height;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(entry.color.size()); ++index)
		{
			if (!entry.color[index])
			{
				RenderTargetDesc desc{};
				desc.width = to_graph_dimension(clamped_width);
				desc.height = to_graph_dimension(clamped_height);
				desc.format = RenderTextureFormat::RGBA16_SFLOAT;
				desc.shader_resource = true;
				desc.unordered_access = true;
				desc.name = index == 0 ? "SceneTemporalAAHistoryA" : "SceneTemporalAAHistoryB";
				desc.use_optimized_clear_value = true;
				desc.optimized_clear_color = k_clear_color;
				entry.color[index] = m_renderer->create_render_target(desc);
				ASH_PROCESS_ERROR(entry.color[index] != nullptr);
			}
		}

		out_entry = &entry;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	TemporalAAPassOutputs TemporalAAPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context,
		const TemporalAAConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("TemporalAAPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(TemporalAAPassOutputs, outputs, TemporalAAPassOutputs{}, TemporalAAPassOutputs{});
		outputs.scene_hdr_linear = scene_hdr_linear;

		const TemporalAAConfig sanitized = sanitize_temporal_aa_config(config, make_default_temporal_aa_config());
		ASH_PROCESS_SUCCESS(!sanitized.enabled || !frame.taa_enabled);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_resolve_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler && m_linear_clamp_sampler);
		ASH_PROCESS_ERROR(scene_hdr_linear.is_valid());
		// GBufferD carries MotionVector3D.rgb + TemporalFlags.a (layout index 3).
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() > 3u);
		const RenderGraphTextureRef gbuffer_motion = deferred_resources.gbuffer_targets[3];
		const RenderGraphTextureRef scene_depth = deferred_resources.depth;
		ASH_PROCESS_ERROR(gbuffer_motion.is_valid() && scene_depth.is_valid());
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);

		const uint32_t output_width = std::max<uint32_t>(view_context.output_target->get_width(), 1u);
		const uint32_t output_height = std::max<uint32_t>(view_context.output_target->get_height(), 1u);

		TemporalAAHistoryEntry* history_entry = nullptr;
		const uint64_t history_view_key = view_context.view_id;
		ASH_PROCESS_ERROR(ensure_history_entry(history_view_key, output_width, output_height, history_entry));
		ASH_PROCESS_ERROR(history_entry != nullptr);

		const uint32_t write_index = history_entry->write_index % static_cast<uint32_t>(history_entry->color.size());
		const uint32_t read_index = write_index ^ 1u;
		const bool history_state_compatible =
			history_entry->valid && history_entry->reverse_z == frame.reverse_z;

		RenderGraphTextureRef history_read{};
		if (history_state_compatible)
		{
			history_read = graph.register_external_texture(
				history_entry->color[read_index],
				"SceneTemporalAAHistoryRead",
				RenderGraphAccess::ComputeSRV);
			ASH_PROCESS_ERROR(history_read);
		}
		const RenderGraphTextureRef history_write = graph.register_external_texture(
			history_entry->color[write_index],
			"SceneTemporalAAHistoryWrite",
			RenderGraphAccess::ComputeUAV);
		ASH_PROCESS_ERROR(history_write);

		const RenderGraphTextureRef resolved = graph.create_texture(
			make_color_texture_desc(output_width, output_height),
			"SceneTemporalAAResolved");
		ASH_PROCESS_ERROR(resolved);

		const TemporalAARootConstants constants =
			make_root_constants(frame, output_width, output_height, sanitized, history_state_compatible);

		if (!m_logged_runtime_state)
		{
			m_logged_runtime_state = true;
			HLogInfo(
				"TemporalAA active. jitter_seq={} history_blend={} variance_gamma={} luma_weight={} history_reproject={} output={}x{} debug_view={}.",
				sanitized.jitter_sequence_length,
				sanitized.history_blend,
				sanitized.variance_gamma,
				sanitized.luminance_weighting ? "true" : "false",
				history_state_compatible ? "true" : "false",
				output_width,
				output_height,
				temporal_aa_debug_view_name(sanitized.debug_view));
		}

		const bool history_has_valid_read = history_state_compatible;
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneTemporalAAResolvePass",
			RenderGraphPassFlags::None,
			[scene_hdr_linear, gbuffer_motion, scene_depth, resolved, history_write,
				history_has_valid_read, history_read](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::ComputeSRV);
				pass.read_texture(gbuffer_motion, RenderGraphAccess::ComputeSRV);
				pass.read_texture(scene_depth, RenderGraphAccess::ComputeSRV);
				if (history_has_valid_read)
				{
					pass.read_texture(history_read, RenderGraphAccess::ComputeSRV);
				}
				pass.write_texture(resolved, RenderGraphAccess::ComputeUAV);
				pass.write_texture(history_write, RenderGraphAccess::ComputeUAV);
			},
			[this, scene_hdr_linear, gbuffer_motion, scene_depth, resolved, history_write,
				history_has_valid_read, history_read, history_view_key, constants, output_width, output_height](
					RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneTemporalAAResolvePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> current_target = context.get_texture(scene_hdr_linear);
				std::shared_ptr<RenderTarget> motion_target = context.get_texture(gbuffer_motion);
				std::shared_ptr<RenderTarget> depth_target = context.get_texture(scene_depth);
				std::shared_ptr<RenderTarget> resolved_target = context.get_texture(resolved);
				std::shared_ptr<RenderTarget> history_write_target = context.get_texture(history_write);
				std::shared_ptr<RenderTarget> history_read_target =
					history_has_valid_read ? context.get_texture(history_read) : current_target;
				ASH_PROCESS_ERROR(current_target && motion_target && depth_target && resolved_target && history_write_target && history_read_target);
				ASH_PROCESS_ERROR(m_resolve_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_resolve_program->set_texture("SceneCurrentHDR", current_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_texture("SceneHistoryHDR", history_read_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_texture("SceneGBufferMotion", motion_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_texture("SceneDepth", depth_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_rw_texture("SceneTaaResolveOutput", resolved_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_rw_texture("SceneTaaHistoryWrite", history_write_target));
				ASH_PROCESS_ERROR(m_resolve_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(m_resolve_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_resolve_program.get();
				dispatch.group_count_x = (output_width + 7u) / 8u;
				dispatch.group_count_y = (output_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));

				const auto history_iter = m_history_entries.find(history_view_key);
				if (history_iter != m_history_entries.end())
				{
					history_iter->second.valid = true;
					history_iter->second.write_index ^= 1u;
				}
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		history_entry->reverse_z = frame.reverse_z;
		outputs.resolved = resolved;
		outputs.scene_hdr_linear = resolved;
		outputs.history_valid = history_state_compatible;

		ASH_PROCESS_GUARD_RETURN_END(outputs, TemporalAAPassOutputs{});
	}
}
