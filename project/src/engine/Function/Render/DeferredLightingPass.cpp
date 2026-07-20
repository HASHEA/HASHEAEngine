#include "Function/Render/DeferredLightingPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/RenderScene.h"
#include "Graphics/Shader.h"
#include "Graphics/VertexInputLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_deferred_common_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredCommon.hlsli";
		static constexpr const char* k_deferred_base_emissive_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredBaseEmissive.hlsl";
		static constexpr const char* k_deferred_directional_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredDirectionalLighting.hlsl";
		static constexpr const char* k_deferred_directional_shadowed_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredDirectionalLightingShadowed.hlsl";
		static constexpr const char* k_deferred_point_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredPointLighting.hlsl";
		static constexpr const char* k_deferred_spot_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredSpotLighting.hlsl";
		static constexpr const char* k_deferred_composite_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredComposite.hlsl";
		static constexpr RenderColorValue k_lighting_accum_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };
		static constexpr RenderColorValue k_scene_hdr_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct DeferredLightingVertex
		{
			glm::vec3 position{};
		};

		struct DeferredLightingRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 light_local_to_clip{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 light_position_and_range{ 0.0f };
			glm::vec4 light_direction_and_intensity{ 0.0f };
			glm::vec4 light_color_and_type{ 0.0f };
			glm::vec4 light_cone_cos{ 1.0f, 1.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(DeferredLightingRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		static auto make_deferred_volume_vertex_input_layout() -> RHI::VertexInputCreation
		{
			constexpr std::array<RHI::VertexStreamDesc, 1> streams = {
				RHI::VertexStreamDesc{
					0,
					static_cast<uint16_t>(sizeof(DeferredLightingVertex)),
					RHI::AshVertexInputRate::PerVertex
				}
			};
			constexpr std::array<RHI::VertexAttributeDesc, 1> attributes = {
				RHI::VertexAttributeDesc{
					0,
					0,
					static_cast<uint32_t>(offsetof(DeferredLightingVertex, position)),
					RHI::AshVertexComponentFormat::Float3,
					RHI::AshVertexSemantic::Position,
					0,
					"POSITION"
				}
			};
			return RHI::make_vertex_input_layout(streams, attributes);
		}

		static auto build_deferred_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_deferred_common_shader_path);
			return hash_value;
		}

		static void apply_view_context_to_draw_desc(
			GraphicsDrawDesc& draw_desc,
			const SceneRenderViewContext& view_context)
		{
			draw_desc.reverse_z = view_context.reverse_z;
			draw_desc.has_viewport = view_context.has_viewport;
			if (view_context.has_viewport)
			{
				draw_desc.viewport = view_context.viewport;
			}
			draw_desc.has_scissor = view_context.has_scissor;
			if (view_context.has_scissor)
			{
				draw_desc.scissor = view_context.scissor;
			}
		}

		static auto get_light_type_id(LightType type) -> float
		{
			switch (type)
			{
			case LightType::Point:
				return 1.0f;
			case LightType::Spot:
				return 2.0f;
			case LightType::Directional:
			default:
				return 0.0f;
			}
		}

		static auto make_light_basis(const glm::vec3& direction_ws) -> glm::mat4
		{
			const glm::vec3 fallback_forward{ 0.0f, 0.0f, 1.0f };
			const float direction_length = glm::length(direction_ws);
			const glm::vec3 forward = direction_length > 0.0001f ? direction_ws / direction_length : fallback_forward;
			const glm::vec3 up_seed =
				std::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
				glm::vec3(1.0f, 0.0f, 0.0f) :
				glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::vec3 right = glm::normalize(glm::cross(up_seed, forward));
			const glm::vec3 up = glm::normalize(glm::cross(forward, right));

			glm::mat4 basis{ 1.0f };
			basis[0] = glm::vec4(right, 0.0f);
			basis[1] = glm::vec4(up, 0.0f);
			basis[2] = glm::vec4(forward, 0.0f);
			return basis;
		}

		static auto make_common_root_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target) -> DeferredLightingRootConstants
		{
			DeferredLightingRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			constants.light_local_to_clip = glm::mat4(1.0f);
			const float width = output_target ? static_cast<float>(output_target->get_width()) : 1.0f;
			const float height = output_target ? static_cast<float>(output_target->get_height()) : 1.0f;
			constants.viewport_size = {
				std::max(width, 1.0f),
				std::max(height, 1.0f),
				1.0f / std::max(width, 1.0f),
				1.0f / std::max(height, 1.0f)
			};
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);
			return constants;
		}

		static void attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const DeferredLightingRootConstants& constants)
		{
			RHI::ShaderParameterBlockLayout layout{};
			if (!program || !program->get_parameter_block_layout("AshRootConstants", layout) || layout.byte_size == 0)
			{
				return;
			}
			draw_desc.const_data_size = std::min<uint32_t>(
				static_cast<uint32_t>(sizeof(constants)),
				std::min<uint32_t>(layout.byte_size, GraphicsDrawDesc::InlineConstDataCapacity));
			draw_desc.inline_const_data_valid = true;
			std::memcpy(draw_desc.inline_const_data.data(), &constants, draw_desc.const_data_size);
		}

		static auto make_directional_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			const VisibleLightData& light) -> DeferredLightingRootConstants
		{
			DeferredLightingRootConstants constants = make_common_root_constants(frame, output_target);
			constants.light_direction_and_intensity = glm::vec4(light.direction_ws, light.intensity);
			constants.light_color_and_type = glm::vec4(light.color, get_light_type_id(light.type));
			return constants;
		}

		static auto make_point_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			const VisibleLightData& light) -> DeferredLightingRootConstants
		{
			DeferredLightingRootConstants constants = make_common_root_constants(frame, output_target);
			const glm::mat4 local_to_world =
				glm::translate(glm::mat4(1.0f), light.position_ws) *
				glm::scale(glm::mat4(1.0f), glm::vec3(std::max(light.range, 0.001f)));
			constants.light_local_to_clip = frame.view_projection * local_to_world;
			constants.light_position_and_range = glm::vec4(light.position_ws, light.range);
			constants.light_direction_and_intensity = glm::vec4(light.direction_ws, light.intensity);
			constants.light_color_and_type = glm::vec4(light.color, get_light_type_id(light.type));
			return constants;
		}

		static auto make_spot_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			const VisibleLightData& light) -> DeferredLightingRootConstants
		{
			DeferredLightingRootConstants constants = make_common_root_constants(frame, output_target);
			const float outer_cos = std::clamp(light.outer_cone_cos, 0.001f, 0.9999f);
			const float outer_sin = std::sqrt(std::max(1.0f - outer_cos * outer_cos, 0.0f));
			const float cone_radius = std::max(light.range, 0.001f) * (outer_sin / outer_cos);
			const glm::mat4 local_to_world =
				glm::translate(glm::mat4(1.0f), light.position_ws) *
				make_light_basis(light.direction_ws) *
				glm::scale(glm::mat4(1.0f), glm::vec3(cone_radius, cone_radius, std::max(light.range, 0.001f)));
			constants.light_local_to_clip = frame.view_projection * local_to_world;
			constants.light_position_and_range = glm::vec4(light.position_ws, light.range);
			constants.light_direction_and_intensity = glm::vec4(light.direction_ws, light.intensity);
			constants.light_color_and_type = glm::vec4(light.color, get_light_type_id(light.type));
			constants.light_cone_cos = glm::vec4(light.inner_cone_cos, light.outer_cone_cos, 0.0f, 0.0f);
			return constants;
		}

		static auto make_program_desc(
			const char* shader_path,
			const char* name,
			const GraphicsProgramState& state,
			const RHI::VertexInputCreation& vertex_input = {}) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_deferred_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			desc.vertex_input = vertex_input;
			return desc;
		}

		static auto create_fullscreen_draw(
			GraphicsProgram* program,
			const DeferredLightingRootConstants& constants,
			const SceneRenderViewContext& view_context,
			bool bind_constants = true) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_count = 3u;
			draw_desc.instance_count = 1u;
			if (bind_constants)
			{
				attach_root_constants(draw_desc, program, constants);
			}
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		static auto create_volume_draw(
			GraphicsProgram* program,
			const std::shared_ptr<VertexBuffer>& vertex_buffer,
			const std::shared_ptr<IndexBuffer>& index_buffer,
			uint32_t index_count,
			const DeferredLightingRootConstants& constants,
			const SceneRenderViewContext& view_context) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_buffers.push_back({ 0u, vertex_buffer, 0u });
			draw_desc.index_buffer = index_buffer;
			draw_desc.index_count = index_count;
			draw_desc.instance_count = 1u;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		static auto create_sphere_vertices(
			std::vector<DeferredLightingVertex>& out_vertices,
			std::vector<uint32_t>& out_indices) -> void
		{
			static constexpr uint32_t k_segments = 32u;
			static constexpr uint32_t k_rings = 16u;
			out_vertices.clear();
			out_indices.clear();
			out_vertices.reserve((k_rings + 1u) * (k_segments + 1u));
			for (uint32_t ring = 0; ring <= k_rings; ++ring)
			{
				const float v = static_cast<float>(ring) / static_cast<float>(k_rings);
				const float theta = v * glm::pi<float>();
				const float y = std::cos(theta);
				const float radius = std::sin(theta);
				for (uint32_t segment = 0; segment <= k_segments; ++segment)
				{
					const float u = static_cast<float>(segment) / static_cast<float>(k_segments);
					const float phi = u * glm::two_pi<float>();
					out_vertices.push_back({ glm::vec3(radius * std::cos(phi), y, radius * std::sin(phi)) });
				}
			}

			for (uint32_t ring = 0; ring < k_rings; ++ring)
			{
				for (uint32_t segment = 0; segment < k_segments; ++segment)
				{
					const uint32_t row0 = ring * (k_segments + 1u);
					const uint32_t row1 = (ring + 1u) * (k_segments + 1u);
					const uint32_t a = row0 + segment;
					const uint32_t b = row0 + segment + 1u;
					const uint32_t c = row1 + segment;
					const uint32_t d = row1 + segment + 1u;
					out_indices.insert(out_indices.end(), { a, c, b, b, c, d });
				}
			}
		}

		static auto create_cone_vertices(
			std::vector<DeferredLightingVertex>& out_vertices,
			std::vector<uint32_t>& out_indices) -> void
		{
			static constexpr uint32_t k_segments = 32u;
			out_vertices.clear();
			out_indices.clear();
			out_vertices.reserve(k_segments + 2u);
			out_vertices.push_back({ glm::vec3(0.0f, 0.0f, 0.0f) });
			for (uint32_t segment = 0; segment < k_segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(k_segments);
				const float phi = u * glm::two_pi<float>();
				out_vertices.push_back({ glm::vec3(std::cos(phi), std::sin(phi), 1.0f) });
			}
			const uint32_t base_center_index = static_cast<uint32_t>(out_vertices.size());
			out_vertices.push_back({ glm::vec3(0.0f, 0.0f, 1.0f) });

			for (uint32_t segment = 0; segment < k_segments; ++segment)
			{
				const uint32_t current = 1u + segment;
				const uint32_t next = 1u + ((segment + 1u) % k_segments);
				out_indices.insert(out_indices.end(), { 0u, next, current });
				out_indices.insert(out_indices.end(), { base_center_index, current, next });
			}
		}
	}

	bool DeferredLightingPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer->create_sampler(sampler_desc, "SceneDeferredPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_ERROR(create_volume_meshes(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::create_programs(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::create_programs", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		GraphicsProgramState fullscreen_state{};
		fullscreen_state.cull_mode = RenderCullMode::None;
		fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		fullscreen_state.depth_test = false;
		fullscreen_state.depth_write = false;
		fullscreen_state.blend_mode = RenderBlendMode::Opaque;

		GraphicsProgramState additive_fullscreen_state = fullscreen_state;
		additive_fullscreen_state.blend_mode = RenderBlendMode::Additive;

		GraphicsProgramState light_volume_state = additive_fullscreen_state;
		light_volume_state.depth_test = true;
		light_volume_state.depth_write = false;
		light_volume_state.depth_compare = RenderCompareOp::GreaterEqual;

		m_base_emissive_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_base_emissive_shader_path,
			"SceneDeferredBaseEmissive",
			additive_fullscreen_state));
		ASH_PROCESS_ERROR(m_base_emissive_program != nullptr);

		m_directional_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_directional_shader_path,
			"SceneDeferredDirectionalLighting",
			additive_fullscreen_state));
		ASH_PROCESS_ERROR(m_directional_program != nullptr);

		m_shadowed_directional_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_directional_shadowed_shader_path,
			"SceneDeferredDirectionalLightingShadowed",
			additive_fullscreen_state));
		ASH_PROCESS_ERROR(m_shadowed_directional_program != nullptr);

		const RHI::VertexInputCreation volume_vertex_input = make_deferred_volume_vertex_input_layout();
		m_point_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_point_shader_path,
			"SceneDeferredPointLighting",
			light_volume_state,
			volume_vertex_input));
		ASH_PROCESS_ERROR(m_point_program != nullptr);

		m_spot_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_spot_shader_path,
			"SceneDeferredSpotLighting",
			light_volume_state,
			volume_vertex_input));
		ASH_PROCESS_ERROR(m_spot_program != nullptr);

		m_composite_program = renderer.create_graphics_program(make_program_desc(
			k_deferred_composite_shader_path,
			"SceneDeferredComposite",
			fullscreen_state));
		ASH_PROCESS_ERROR(m_composite_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::create_volume_meshes(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::create_volume_meshes", AshEngine::Profile::Color::Upload);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		std::vector<DeferredLightingVertex> vertices{};
		std::vector<uint32_t> indices{};

		create_sphere_vertices(vertices, indices);
		VertexBufferDesc sphere_vertex_desc{};
		sphere_vertex_desc.size = static_cast<uint32_t>(vertices.size() * sizeof(DeferredLightingVertex));
		sphere_vertex_desc.stride = static_cast<uint32_t>(sizeof(DeferredLightingVertex));
		sphere_vertex_desc.initial_data = vertices.data();
		sphere_vertex_desc.name = "SceneDeferredPointLightSphereVB";
		m_sphere_vertex_buffer = renderer.create_vertex_buffer(sphere_vertex_desc);
		ASH_PROCESS_ERROR(m_sphere_vertex_buffer != nullptr);

		IndexBufferDesc sphere_index_desc{};
		sphere_index_desc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
		sphere_index_desc.format = RenderIndexFormat::UInt32;
		sphere_index_desc.initial_data = indices.data();
		sphere_index_desc.name = "SceneDeferredPointLightSphereIB";
		m_sphere_index_buffer = renderer.create_index_buffer(sphere_index_desc);
		ASH_PROCESS_ERROR(m_sphere_index_buffer != nullptr);
		m_sphere_index_count = static_cast<uint32_t>(indices.size());

		create_cone_vertices(vertices, indices);
		VertexBufferDesc cone_vertex_desc{};
		cone_vertex_desc.size = static_cast<uint32_t>(vertices.size() * sizeof(DeferredLightingVertex));
		cone_vertex_desc.stride = static_cast<uint32_t>(sizeof(DeferredLightingVertex));
		cone_vertex_desc.initial_data = vertices.data();
		cone_vertex_desc.name = "SceneDeferredSpotLightConeVB";
		m_cone_vertex_buffer = renderer.create_vertex_buffer(cone_vertex_desc);
		ASH_PROCESS_ERROR(m_cone_vertex_buffer != nullptr);

		IndexBufferDesc cone_index_desc{};
		cone_index_desc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
		cone_index_desc.format = RenderIndexFormat::UInt32;
		cone_index_desc.initial_data = indices.data();
		cone_index_desc.name = "SceneDeferredSpotLightConeIB";
		m_cone_index_buffer = renderer.create_index_buffer(cone_index_desc);
		ASH_PROCESS_ERROR(m_cone_index_buffer != nullptr);
		m_cone_index_count = static_cast<uint32_t>(indices.size());
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void DeferredLightingPass::shutdown()
	{
		m_cone_index_count = 0;
		m_cone_index_buffer.reset();
		m_cone_vertex_buffer.reset();
		m_sphere_index_count = 0;
		m_sphere_index_buffer.reset();
		m_sphere_vertex_buffer.reset();
		m_composite_program.reset();
		m_spot_program.reset();
		m_point_program.reset();
		m_directional_program.reset();
		m_shadowed_directional_program.reset();
		m_base_emissive_program.reset();
		m_point_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool DeferredLightingPass::add_base_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_base_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_base_emissive_program && m_directional_program && m_shadowed_directional_program && m_point_program && m_spot_program && m_composite_program);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDeferredLightingBasePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Clear, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Clear, k_lighting_accum_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, output_target](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredLightingBasePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				const std::vector<RenderGraphTextureRef>& gbuffer_refs = deferred_resources.gbuffer_targets;
				std::shared_ptr<RenderTarget> gbuffer_a = context.get_texture(gbuffer_refs[0]);
				std::shared_ptr<RenderTarget> gbuffer_b = context.get_texture(gbuffer_refs[1]);
				std::shared_ptr<RenderTarget> gbuffer_c = context.get_texture(gbuffer_refs[2]);
				std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(gbuffer_refs[3]);
				std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(gbuffer_refs[4]);
				std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> ambient_occlusion = context.get_texture(deferred_resources.ambient_occlusion);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(gbuffer_a && gbuffer_b && gbuffer_c && gbuffer_d && gbuffer_e && depth && ambient_occlusion && output);

				for (GraphicsProgram* program : {
					m_base_emissive_program.get(),
					m_directional_program.get(),
					m_shadowed_directional_program.get(),
					m_point_program.get(),
					m_spot_program.get()
				})
				{
					ASH_PROCESS_ERROR(program != nullptr);
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferA", gbuffer_a));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferB", gbuffer_b));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferC", gbuffer_c));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferD", gbuffer_d));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferE", gbuffer_e));
					ASH_PROCESS_ERROR(program->set_texture("SceneDepth", depth));
					ASH_PROCESS_ERROR(program->set_texture("SceneAmbientOcclusion", ambient_occlusion));
					ASH_PROCESS_ERROR(program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				}

				const DeferredLightingRootConstants base_constants = make_common_root_constants(frame, output);
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_base_emissive_program.get(),
					base_constants,
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_directional_light_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context,
		uint32_t frame_light_index,
		RenderGraphTextureRef shadow_mask)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_directional_light_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(frame_light_index < frame.lights.size());
		const VisibleLightData light = frame.lights[frame_light_index];
		ASH_PROCESS_ERROR(light.type == LightType::Directional);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_directional_program && m_shadowed_directional_program);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		const bool use_directional_shadow = shadow_mask.is_valid();
		const std::string light_pass_name =
			use_directional_shadow ?
			"SceneDeferredDirectionalLightingShadowedPass_" + std::to_string(frame_light_index) :
			"SceneDeferredDirectionalLightingPass_" + std::to_string(frame_light_index);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			light_pass_name.c_str(),
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				if (use_directional_shadow)
				{
					pass.read_texture(shadow_mask, RenderGraphAccess::GraphicsSRV);
				}
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
			},
			[this, &frame, &view_context, output_target, light, use_directional_shadow, shadow_mask](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredDirectionalLightingPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(output != nullptr);
				if (use_directional_shadow)
				{
					std::shared_ptr<RenderTarget> resolved_shadow_mask = context.get_texture(shadow_mask);
					ASH_PROCESS_ERROR(resolved_shadow_mask != nullptr);
					ASH_PROCESS_ERROR(m_shadowed_directional_program->set_texture("SceneDirectionalShadowMask", resolved_shadow_mask));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
						m_shadowed_directional_program.get(),
						make_directional_constants(frame, output, light),
						view_context)));
				}
				else
				{
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
						m_directional_program.get(),
						make_directional_constants(frame, output, light),
						view_context)));
				}
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_point_light_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context,
		uint32_t frame_light_index)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_point_light_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(frame_light_index < frame.lights.size());
		const VisibleLightData light = frame.lights[frame_light_index];
		ASH_PROCESS_ERROR(light.type == LightType::Point);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_point_program != nullptr);
		ASH_PROCESS_ERROR(m_sphere_vertex_buffer && m_sphere_index_buffer && m_sphere_index_count > 0);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		const std::string light_pass_name = "SceneDeferredPointLightingPass_" + std::to_string(frame_light_index);
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			light_pass_name.c_str(),
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
			},
			[this, &frame, &view_context, output_target, light](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredPointLightingPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(output != nullptr);
				ASH_PROCESS_ERROR(context.draw(create_volume_draw(
					m_point_program.get(),
					m_sphere_vertex_buffer,
					m_sphere_index_buffer,
					m_sphere_index_count,
					make_point_constants(frame, output, light),
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_spot_light_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context,
		uint32_t frame_light_index)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_spot_light_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(frame_light_index < frame.lights.size());
		const VisibleLightData light = frame.lights[frame_light_index];
		ASH_PROCESS_ERROR(light.type == LightType::Spot);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_spot_program != nullptr);
		ASH_PROCESS_ERROR(m_cone_vertex_buffer && m_cone_index_buffer && m_cone_index_count > 0);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		const std::string light_pass_name = "SceneDeferredSpotLightingPass_" + std::to_string(frame_light_index);
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			light_pass_name.c_str(),
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
			},
			[this, &frame, &view_context, output_target, light](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredSpotLightingPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(output != nullptr);
				ASH_PROCESS_ERROR(context.draw(create_volume_draw(
					m_spot_program.get(),
					m_cone_vertex_buffer,
					m_cone_index_buffer,
					m_cone_index_count,
					make_spot_constants(frame, output, light),
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_lighting_accumulation_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context,
		SunLightShadowPass* directional_shadow_pass,
		const SunLightShadowPassOutputs* sunlight_shadow_outputs)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_lighting_accumulation_pass", AshEngine::Profile::Color::Scene);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.lights.size()));
		ASH_PROFILE_PLOT("Scene/VisibleLights", static_cast<int64_t>(frame.lights.size()));
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_base_emissive_program && m_directional_program && m_shadowed_directional_program && m_point_program && m_spot_program && m_composite_program);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(m_sphere_vertex_buffer && m_sphere_index_buffer && m_sphere_index_count > 0);
		ASH_PROCESS_ERROR(m_cone_vertex_buffer && m_cone_index_buffer && m_cone_index_count > 0);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDeferredLightingBasePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Clear, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Clear, k_lighting_accum_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, output_target](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredLightingBasePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				const std::vector<RenderGraphTextureRef>& gbuffer_refs = deferred_resources.gbuffer_targets;
				std::shared_ptr<RenderTarget> gbuffer_a = context.get_texture(gbuffer_refs[0]);
				std::shared_ptr<RenderTarget> gbuffer_b = context.get_texture(gbuffer_refs[1]);
				std::shared_ptr<RenderTarget> gbuffer_c = context.get_texture(gbuffer_refs[2]);
				std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(gbuffer_refs[3]);
				std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(gbuffer_refs[4]);
				std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> ambient_occlusion = context.get_texture(deferred_resources.ambient_occlusion);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(gbuffer_a && gbuffer_b && gbuffer_c && gbuffer_d && gbuffer_e && depth && ambient_occlusion && output);

				for (GraphicsProgram* program : {
					m_base_emissive_program.get(),
					m_directional_program.get(),
					m_shadowed_directional_program.get(),
					m_point_program.get(),
					m_spot_program.get()
				})
				{
					ASH_PROCESS_ERROR(program != nullptr);
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferA", gbuffer_a));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferB", gbuffer_b));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferC", gbuffer_c));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferD", gbuffer_d));
					ASH_PROCESS_ERROR(program->set_texture("SceneGBufferE", gbuffer_e));
					ASH_PROCESS_ERROR(program->set_texture("SceneDepth", depth));
					ASH_PROCESS_ERROR(program->set_texture("SceneAmbientOcclusion", ambient_occlusion));
					ASH_PROCESS_ERROR(program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				}

				const DeferredLightingRootConstants base_constants = make_common_root_constants(frame, output);
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_base_emissive_program.get(),
					base_constants,
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
		{
			const VisibleLightData& light = frame.lights[light_index];
			const DirectionalShadowLightPlan* shadow_plan =
				sunlight_shadow_outputs ?
				find_shadow_plan_for_frame_light(*sunlight_shadow_outputs, light_index) :
				nullptr;
			const bool use_directional_shadow =
				light.type == LightType::Directional &&
				shadow_plan &&
				shadow_plan->shadowed &&
				directional_shadow_pass &&
				sunlight_shadow_outputs &&
				sunlight_shadow_outputs->has_shadowed_lights();

			if (use_directional_shadow)
			{
				ASH_PROCESS_ERROR(directional_shadow_pass->add_shadow_mask_pass(
					graph,
					*sunlight_shadow_outputs,
					shadow_plan->light_plan_index,
					frame,
					deferred_resources,
					view_context));
			}

			const std::string light_pass_name =
				use_directional_shadow ?
				"SceneDeferredDirectionalLightingShadowedPass_" + std::to_string(light_index) :
				(light.type == LightType::Directional ?
					"SceneDeferredDirectionalLightingPass_" + std::to_string(light_index) :
					(light.type == LightType::Point ?
						"SceneDeferredPointLightingPass_" + std::to_string(light_index) :
						"SceneDeferredSpotLightingPass_" + std::to_string(light_index)));

			ASH_PROCESS_ERROR(graph.add_raster_pass(
				light_pass_name.c_str(),
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::DeferredLighting,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					if (use_directional_shadow)
					{
						pass.read_texture(sunlight_shadow_outputs->shadow_mask, RenderGraphAccess::GraphicsSRV);
					}
					pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
					pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
				},
				[this, &frame, &deferred_resources, &view_context, output_target, light, use_directional_shadow, sunlight_shadow_outputs](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneDeferredLightingLightPass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
					ASH_PROCESS_ERROR(output != nullptr);

					if (light.type == LightType::Directional)
					{
						if (use_directional_shadow)
						{
							std::shared_ptr<RenderTarget> shadow_mask = context.get_texture(sunlight_shadow_outputs->shadow_mask);
							ASH_PROCESS_ERROR(shadow_mask != nullptr);
							ASH_PROCESS_ERROR(m_shadowed_directional_program->set_texture("SceneDirectionalShadowMask", shadow_mask));
							ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
								m_shadowed_directional_program.get(),
								make_directional_constants(frame, output, light),
								view_context)));
						}
						else
						{
							ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
								m_directional_program.get(),
								make_directional_constants(frame, output, light),
								view_context)));
						}
					}
					else if (light.type == LightType::Point)
					{
						ASH_PROCESS_ERROR(context.draw(create_volume_draw(
							m_point_program.get(),
							m_sphere_vertex_buffer,
							m_sphere_index_buffer,
							m_sphere_index_count,
							make_point_constants(frame, output, light),
							view_context)));
					}
					else if (light.type == LightType::Spot)
					{
						ASH_PROCESS_ERROR(context.draw(create_volume_draw(
							m_spot_program.get(),
							m_cone_vertex_buffer,
							m_cone_index_buffer,
							m_cone_index_count,
							make_spot_constants(frame, output, light),
							view_context)));
					}
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				}));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_composite_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_composite_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_composite_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(output_target);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);
		ASH_PROCESS_ERROR(deferred_resources.scene_hdr_linear);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDeferredCompositePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::EnvironmentAndSky,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(deferred_resources.lighting_diffuse, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(deferred_resources.lighting_specular, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, deferred_resources.scene_hdr_linear, RenderLoadAction::Clear, k_scene_hdr_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, output_target](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredCompositePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> lighting_diffuse = context.get_texture(deferred_resources.lighting_diffuse);
				std::shared_ptr<RenderTarget> lighting_specular = context.get_texture(deferred_resources.lighting_specular);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(lighting_diffuse && lighting_specular && output);
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneLightingDiffuse", lighting_diffuse));
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneLightingSpecular", lighting_specular));
				ASH_PROCESS_ERROR(m_composite_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_composite_program.get(),
					make_common_root_constants(frame, output),
					view_context,
					false)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DeferredLightingPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context,
		SunLightShadowPass* directional_shadow_pass,
		const SunLightShadowPassOutputs* sunlight_shadow_outputs)
	{
		ASH_PROFILE_SCOPE_NC("DeferredLightingPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(add_lighting_accumulation_pass(
			graph,
			frame,
			deferred_resources,
			output_target,
			view_context,
			directional_shadow_pass,
			sunlight_shadow_outputs));
		ASH_PROCESS_ERROR(add_composite_pass(
			graph,
			frame,
			deferred_resources,
			output_target,
			view_context));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
