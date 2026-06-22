#include "Function/Render/SceneRenderer.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Application.h"
#include "Function/Render/DebugDrawService.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/GBufferLayout.h"
#include "Function/Render/MaterialRenderProxy.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/ScenePresentationHandles.h"
#include "Function/Render/TemporalAAConfig.h"
#include "Function/Render/TemporalAAPass.h"
#include "Function/Render/VertexLayoutPresets.h"
#include "Function/Scene/SceneQuery.h"
#include "Function/Asset/AssetDatabase.h"
#include "Graphics/Shader.h"
#include "Graphics/VertexInputLayout.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_debug_draw_shader_path =
			"project/src/engine/Shaders/Debug/DebugDrawOverlay.hlsl";
		static constexpr const char* k_entity_pick_shader_path =
			"project/src/engine/Shaders/Scene/SceneEntityPick.hlsl";
		static constexpr const char* k_vertex_decl_locations_shader_path =
			"project/src/engine/Graphics/Shaders/AshVertexDeclLocations.hlsli";
		static constexpr size_t k_scene_instance_buffer_frame_lag = 3;

		struct DebugDrawVertex
		{
			glm::vec3 position_ws{ 0.0f };
			glm::vec4 color{ 1.0f };
		};

		struct DebugDrawRootConstants
		{
			glm::mat4 view_projection{ 1.0f };
			float depth_bias = 0.0f;
			glm::vec3 padding{ 0.0f };
		};

		static_assert(sizeof(DebugDrawRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		struct SceneEntityPickRootConstants
		{
			glm::uvec2 entity_id{ 0u, 0u };
			glm::uvec2 padding{ 0u, 0u };
		};

		static_assert(sizeof(SceneEntityPickRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		static auto pack_entity_id(EntityId entity_id) -> glm::uvec2
		{
			return {
				static_cast<uint32_t>(entity_id & 0xFFFFFFFFull),
				static_cast<uint32_t>(entity_id >> 32)
			};
		}

		static auto build_entity_pick_shader_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_entity_pick_shader_path);
			RHI::hash_shader_file_signature(hash_value, k_vertex_decl_locations_shader_path);
			return hash_value;
		}

		static auto make_entity_pick_program_desc() -> GraphicsProgramDesc
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::Back;
			state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			state.depth_test = true;
			state.depth_write = false;
			state.depth_compare = RenderCompareOp::LessEqual;
			state.blend_mode = RenderBlendMode::Opaque;

			GraphicsProgramDesc desc{};
			desc.shader_path = k_entity_pick_shader_path;
			desc.base_shader_path = k_entity_pick_shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_entity_pick_shader_source_hash();
			desc.name = "SceneEntityPick";
			desc.state = state;
			desc.vertex_decl = get_instanced_mesh_vertex_decl();
			desc.vertex_input = make_instanced_mesh_vertex_input_layout();
			return desc;
		}

		static void attach_entity_pick_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const SceneEntityPickRootConstants& constants)
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

		struct SceneOverlayDrawBatch
		{
			SceneOverlayDepthMode depth_mode = SceneOverlayDepthMode::DepthTestNoWrite;
			float depth_bias = 0.0f;
			float color_alpha_scale = 1.0f;
			bool xray_ghost = false;
			std::vector<DebugDrawVertex> vertices{};
		};

		static auto build_material_label(const MaterialInterface& material) -> std::string
		{
			if (!material.get_asset_path().empty())
			{
				return material.get_asset_path().generic_string();
			}
			if (!material.get_name().empty())
			{
				return material.get_name();
			}
			return "<unnamed-material>";
		}

		static auto make_debug_draw_vertex_input_layout() -> RHI::VertexInputCreation
		{
			constexpr std::array<RHI::VertexStreamDesc, 1> streams = {
				RHI::VertexStreamDesc{
					0,
					static_cast<uint16_t>(sizeof(DebugDrawVertex)),
					RHI::AshVertexInputRate::PerVertex
				}
			};
			constexpr std::array<RHI::VertexAttributeDesc, 2> attributes = {
				RHI::VertexAttributeDesc{
					0,
					0,
					static_cast<uint32_t>(offsetof(DebugDrawVertex, position_ws)),
					RHI::AshVertexComponentFormat::Float3,
					RHI::AshVertexSemantic::Position,
					0,
					"POSITION"
				},
				RHI::VertexAttributeDesc{
					1,
					0,
					static_cast<uint32_t>(offsetof(DebugDrawVertex, color)),
					RHI::AshVertexComponentFormat::Float4,
					RHI::AshVertexSemantic::Color0,
					0,
					"COLOR"
				}
			};
			return RHI::make_vertex_input_layout(streams, attributes);
		}

		static auto build_debug_draw_shader_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_debug_draw_shader_path);
			RHI::hash_shader_file_signature(hash_value, k_vertex_decl_locations_shader_path);
			return hash_value;
		}

		static auto make_debug_draw_program_desc(
			bool depth_test,
			bool depth_write,
			RenderBlendMode blend_mode = RenderBlendMode::Opaque) -> GraphicsProgramDesc
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::None;
			state.primitive_topology = RenderPrimitiveTopology::LineList;
			state.depth_test = depth_test;
			state.depth_write = depth_write;
			state.depth_compare = RenderCompareOp::LessEqual;
			state.blend_mode = blend_mode;

			GraphicsProgramDesc desc{};
			desc.shader_path = k_debug_draw_shader_path;
			desc.base_shader_path = k_debug_draw_shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_debug_draw_shader_source_hash();
			desc.name = depth_test ?
				(depth_write ? "SceneOverlayDepthTest" : "SceneOverlayDepthTestNoWrite") :
				"SceneDebugDrawOverlay";
			desc.state = state;
			desc.vertex_input = make_debug_draw_vertex_input_layout();
			return desc;
		}

		static auto make_debug_draw_program_desc() -> GraphicsProgramDesc
		{
			return make_debug_draw_program_desc(false, false, RenderBlendMode::Opaque);
		}

		static void attach_debug_draw_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const DebugDrawRootConstants& constants)
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

		static void append_scene_overlay_line_vertices(
			std::vector<DebugDrawVertex>& vertices,
			const SceneOverlayLine& line,
			float color_alpha_scale,
			bool xray_ghost)
		{
			glm::vec4 color = line.color;
			color.a *= color_alpha_scale;
			if (xray_ghost)
			{
				color.r *= 0.35f;
				color.g *= 0.35f;
				color.b *= 0.35f;
			}

			vertices.push_back({ line.start, color });
			vertices.push_back({ line.end, color });
		}

		static auto build_scene_overlay_draw_batches(
			const std::vector<SceneOverlayLine>& lines) -> std::vector<SceneOverlayDrawBatch>
		{
			std::vector<SceneOverlayDrawBatch> batches{};
			auto find_or_add_batch =
				[&batches](
					SceneOverlayDepthMode depth_mode,
					float depth_bias,
					float color_alpha_scale,
					bool xray_ghost) -> SceneOverlayDrawBatch&
			{
				for (SceneOverlayDrawBatch& batch : batches)
				{
					if (batch.depth_mode == depth_mode &&
						batch.depth_bias == depth_bias &&
						batch.color_alpha_scale == color_alpha_scale &&
						batch.xray_ghost == xray_ghost)
					{
						return batch;
					}
				}

				SceneOverlayDrawBatch batch{};
				batch.depth_mode = depth_mode;
				batch.depth_bias = depth_bias;
				batch.color_alpha_scale = color_alpha_scale;
				batch.xray_ghost = xray_ghost;
				batches.push_back(std::move(batch));
				return batches.back();
			};

			for (const SceneOverlayLine& line : lines)
			{
				switch (line.depth_mode)
				{
				case SceneOverlayDepthMode::AlwaysOnTop:
					append_scene_overlay_line_vertices(
						find_or_add_batch(SceneOverlayDepthMode::AlwaysOnTop, 0.0f, 1.0f, false).vertices,
						line,
						1.0f,
						false);
					break;
				case SceneOverlayDepthMode::DepthTest:
					append_scene_overlay_line_vertices(
						find_or_add_batch(SceneOverlayDepthMode::DepthTest, line.depth_bias, 1.0f, false).vertices,
						line,
						1.0f,
						false);
					break;
				case SceneOverlayDepthMode::DepthTestNoWrite:
					append_scene_overlay_line_vertices(
						find_or_add_batch(SceneOverlayDepthMode::DepthTestNoWrite, line.depth_bias, 1.0f, false).vertices,
						line,
						1.0f,
						false);
					break;
				case SceneOverlayDepthMode::XRay:
					append_scene_overlay_line_vertices(
						find_or_add_batch(SceneOverlayDepthMode::DepthTestNoWrite, line.depth_bias, 1.0f, false).vertices,
						line,
						1.0f,
						false);
					append_scene_overlay_line_vertices(
						find_or_add_batch(SceneOverlayDepthMode::AlwaysOnTop, 0.0f, 1.0f, true).vertices,
						line,
						1.0f,
						true);
					break;
				default:
					break;
				}
			}

			batches.erase(
				std::remove_if(
					batches.begin(),
					batches.end(),
					[](const SceneOverlayDrawBatch& batch) { return batch.vertices.empty(); }),
				batches.end());
			return batches;
		}

		static auto make_debug_draw_vertices(const std::vector<DebugDrawLine>& lines) -> std::vector<DebugDrawVertex>
		{
			std::vector<DebugDrawVertex> vertices{};
			vertices.reserve(lines.size() * 2u);
			for (const DebugDrawLine& line : lines)
			{
				vertices.push_back({ line.start, line.color });
				vertices.push_back({ line.end, line.color });
			}
			return vertices;
		}

		static auto ensure_debug_draw_vertex_buffer(
			Renderer& renderer,
			std::shared_ptr<VertexBuffer>& vertex_buffer,
			uint32_t& vertex_capacity,
			const std::vector<DebugDrawVertex>& vertices) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(!vertices.empty());
			ASH_PROCESS_ERROR(vertices.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
			const uint32_t vertex_count = static_cast<uint32_t>(vertices.size());
			const size_t required_size = vertices.size() * sizeof(DebugDrawVertex);
			ASH_PROCESS_ERROR(required_size <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));

			if (!vertex_buffer || vertex_capacity < vertex_count)
			{
				VertexBufferDesc vertex_desc{};
				vertex_desc.size = static_cast<uint32_t>(required_size);
				vertex_desc.stride = static_cast<uint32_t>(sizeof(DebugDrawVertex));
				vertex_desc.cpu_write = true;
				vertex_desc.initial_data = vertices.data();
				vertex_desc.name = "SceneDebugDrawLineVB";
				vertex_buffer = renderer.create_vertex_buffer(vertex_desc);
				ASH_PROCESS_ERROR(vertex_buffer != nullptr);
				vertex_capacity = vertex_count;
			}
			else
			{
				ASH_PROCESS_ERROR(vertex_buffer->update(0, static_cast<uint32_t>(required_size), vertices.data()));
			}

			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		static auto get_staticmesh_pass_label(PassFamily pass_family) -> const char*
		{
			switch (pass_family)
			{
			case PassFamily::GBuffer:
				return "Surface.StaticMesh.GBuffer";
			case PassFamily::DepthOnly:
				return "Surface.StaticMesh.DepthOnly";
			case PassFamily::BasePass:
			default:
				return "Surface.StaticMesh.BasePass";
			}
		}

		static auto select_staticmesh_material_resource(
			const MaterialRenderProxy& material_proxy,
			PassFamily pass_family) -> const MaterialResource*
		{
			switch (pass_family)
			{
			case PassFamily::GBuffer:
				return material_proxy.get_surface_staticmesh_gbuffer_resource();
			case PassFamily::DepthOnly:
				return material_proxy.get_surface_staticmesh_depthonly_resource();
			case PassFamily::BasePass:
			default:
				return material_proxy.get_surface_staticmesh_basepass_resource();
			}
		}

		static auto supports_staticmesh_pass(
			const MaterialResource& resource,
			PassFamily pass_family) -> bool
		{
			if (!resource.pass_relevance.supports_surface ||
				resource.pass_relevance.domain != MaterialDomain::Surface)
			{
				return false;
			}

			switch (pass_family)
			{
			case PassFamily::GBuffer:
				return resource.pass_relevance.supports_gbuffer_pass;
			case PassFamily::DepthOnly:
				return resource.pass_relevance.supports_depth_prepass;
			case PassFamily::BasePass:
			default:
				return resource.pass_relevance.supports_base_pass;
			}
		}

		struct StaticMeshDrawBatch
		{
			GraphicsProgram* program = nullptr;
			std::shared_ptr<StaticMeshRenderAsset> render_asset = nullptr;
			std::shared_ptr<VertexBuffer> vertex_buffer = nullptr;
			std::shared_ptr<IndexBuffer> index_buffer = nullptr;
			uint32_t first_index = 0;
			uint32_t index_count = 0;
			std::vector<SceneStaticMeshInstanceData> instances{};
		};

		struct StaticMeshDrawBatchKey
		{
			GraphicsProgram* program = nullptr;
			StaticMeshRenderAsset* render_asset = nullptr;
			uint32_t first_index = 0;
			uint32_t index_count = 0;

			bool operator==(const StaticMeshDrawBatchKey& rhs) const
			{
				return program == rhs.program &&
					render_asset == rhs.render_asset &&
					first_index == rhs.first_index &&
					index_count == rhs.index_count;
			}
		};

		struct StaticMeshDrawBatchKeyHash
		{
			size_t operator()(const StaticMeshDrawBatchKey& key) const
			{
				size_t hash_value = reinterpret_cast<uintptr_t>(key.program);
				hash_value ^= reinterpret_cast<uintptr_t>(key.render_asset) + 0x9e3779b97f4a7c15ull + (hash_value << 6) + (hash_value >> 2);
				hash_value ^= static_cast<size_t>(key.first_index) + 0x9e3779b97f4a7c15ull + (hash_value << 6) + (hash_value >> 2);
				hash_value ^= static_cast<size_t>(key.index_count) + 0x9e3779b97f4a7c15ull + (hash_value << 6) + (hash_value >> 2);
				return hash_value;
			}
		};

		static void clear_static_mesh_batch_resource_refs(std::vector<StaticMeshDrawBatch>& batches, size_t active_batch_count)
		{
			const size_t count = std::min(active_batch_count, batches.size());
			for (size_t batch_index = 0; batch_index < count; ++batch_index)
			{
				StaticMeshDrawBatch& batch = batches[batch_index];
				batch.program = nullptr;
				batch.render_asset.reset();
				batch.vertex_buffer.reset();
				batch.index_buffer.reset();
				batch.instances.clear();
			}
		}

		static auto resolve_static_mesh_temporal_key(const VisibleStaticMeshDraw& draw) -> uint64_t
		{
			return draw.entity_id != 0 ? static_cast<uint64_t>(draw.entity_id) : draw.primitive_id;
		}

		static auto resolve_render_frame_index(const VisibleRenderFrame& frame) -> uint64_t
		{
			Application* application = Application::get();
			return application ? application->get_frame_index() : frame.frame_index;
		}

		static auto should_use_temporal_history_for_pass(PassFamily pass_family) -> bool
		{
			return pass_family == PassFamily::GBuffer;
		}

		static auto make_instance_data(
			const glm::mat4& object_to_clip,
			const glm::mat4& previous_object_to_clip,
			bool temporal_valid) -> SceneStaticMeshInstanceData
		{
			SceneStaticMeshInstanceData data{};
			data.object_to_clip_col0 = object_to_clip[0];
			data.object_to_clip_col1 = object_to_clip[1];
			data.object_to_clip_col2 = object_to_clip[2];
			data.object_to_clip_col3 = object_to_clip[3];
			data.previous_object_to_clip_col0 = previous_object_to_clip[0];
			data.previous_object_to_clip_col1 = previous_object_to_clip[1];
			data.previous_object_to_clip_col2 = previous_object_to_clip[2];
			data.previous_object_to_clip_col3 = previous_object_to_clip[3];
			data.temporal_flags = glm::vec4(temporal_valid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
			return data;
		}

		static void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
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

		static void register_render_debug_item(
			RenderDebugView& debug_view,
			const char* name,
			const char* display_name,
			RenderGraphTextureRef texture,
			RenderDebugVisualization visualization,
			RenderTextureFormat format,
			uint32_t width,
			uint32_t height)
		{
			if (!texture)
			{
				return;
			}

			debug_view.register_item({
				name ? name : "",
				display_name ? display_name : "",
				texture,
				visualization,
				format,
				width,
				height });
		}

		static auto validate_static_mesh_draw_asset(const VisibleStaticMeshDraw& draw) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(draw.render_asset && draw.render_asset->is_gpu_ready());
			ASH_PROCESS_ERROR(draw.render_asset->resource);
			ASH_PROCESS_ERROR(draw.render_asset->resource->vertex_decl != nullptr);
			ASH_PROCESS_ERROR(
				RHI::vertex_input_layouts_equal(
					draw.render_asset->resource->vertex_decl->get_vertex_input(),
					get_mesh_vertex_decl()->get_vertex_input()));
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		static auto find_or_add_batch(
			std::vector<StaticMeshDrawBatch>& batches,
			size_t& active_batch_count,
			std::unordered_map<StaticMeshDrawBatchKey, size_t, StaticMeshDrawBatchKeyHash>& batch_lookup,
			GraphicsProgram* program,
			const std::shared_ptr<StaticMeshRenderAsset>& render_asset,
			const ResolvedStaticMeshSection& section) -> StaticMeshDrawBatch&
		{
			StaticMeshDrawBatchKey key{};
			key.program = program;
			key.render_asset = render_asset.get();
			key.first_index = section.first_index;
			key.index_count = section.index_count;
			const auto found = batch_lookup.find(key);
			if (found != batch_lookup.end())
			{
				return batches[found->second];
			}

			if (active_batch_count == batches.size())
			{
				batches.emplace_back();
			}
			StaticMeshDrawBatch& batch = batches[active_batch_count];
			batch.program = program;
			batch.render_asset = render_asset;
			batch.vertex_buffer = render_asset && render_asset->resource ? render_asset->resource->vertex_buffer : nullptr;
			batch.index_buffer = render_asset && render_asset->resource ? render_asset->resource->index_buffer : nullptr;
			batch.first_index = section.first_index;
			batch.index_count = section.index_count;
			batch.instances.clear();
			batch_lookup.emplace(key, active_batch_count);
			++active_batch_count;
			return batch;
		}

	}

	bool SceneRenderer::initialize(Renderer* renderer, DebugDrawService* debug_draw_service)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_renderer = renderer;
		m_debug_draw_service = debug_draw_service;
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_render_debug_view.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_ambient_occlusion_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_sunlight_shadow_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_directional_light_shadow_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_deferred_lighting_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_environment_lighting_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_sky_background_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_volumetric_lighting_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_taa_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_bloom_pass.initialize(m_renderer));
		ASH_PROCESS_ERROR(m_post_process_tone_map_pass.initialize(m_renderer));
		m_debug_draw_program = m_renderer->create_graphics_program(make_debug_draw_program_desc());
		ASH_PROCESS_ERROR(m_debug_draw_program != nullptr);
		m_scene_overlay_depth_test_program =
			m_renderer->create_graphics_program(make_debug_draw_program_desc(true, true, RenderBlendMode::Opaque));
		ASH_PROCESS_ERROR(m_scene_overlay_depth_test_program != nullptr);
		m_scene_overlay_depth_test_no_write_program =
			m_renderer->create_graphics_program(make_debug_draw_program_desc(true, false, RenderBlendMode::Opaque));
		ASH_PROCESS_ERROR(m_scene_overlay_depth_test_no_write_program != nullptr);
		m_entity_pick_program = m_renderer->create_graphics_program(make_entity_pick_program_desc());
		ASH_PROCESS_ERROR(m_entity_pick_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SceneRenderer::shutdown()
	{
		m_debug_draw_vertex_capacity = 0;
		m_scene_overlay_vertex_capacity = 0;
		m_debug_draw_vertex_buffer.reset();
		m_scene_overlay_vertex_buffer.reset();
		m_debug_draw_program.reset();
		m_scene_overlay_depth_test_program.reset();
		m_scene_overlay_depth_test_no_write_program.reset();
		m_entity_pick_program.reset();
		m_entity_pick_target.reset();
		m_entity_pick_width = 0;
		m_entity_pick_height = 0;
		m_pending_pick_readback = {};
		m_debug_draw_service = nullptr;
		m_post_process_tone_map_pass.shutdown();
		m_bloom_pass.shutdown();
		m_taa_pass.shutdown();
		m_volumetric_lighting_pass.shutdown();
		m_sky_background_pass.shutdown();
		m_environment_lighting_pass.shutdown();
		m_deferred_lighting_pass.shutdown();
		m_directional_light_shadow_pass.shutdown();
		m_sunlight_shadow_pass.shutdown();
		m_ambient_occlusion_pass.shutdown();
		m_render_debug_view.shutdown();
		m_instance_buffers.clear();
		m_instance_buffer_frame_index = std::numeric_limits<uint64_t>::max();
		m_next_instance_buffer_slot = 0;
		m_temporal_view_states.clear();
		m_logged_warning_keys.clear();
		m_logged_material_usage_keys.clear();
		m_renderer = nullptr;
	}

	void SceneRenderer::handle_output_resized()
	{
		m_volumetric_lighting_pass.clear_history();
		m_taa_pass.clear_history();
	}

	bool SceneRenderer::should_use_instanced_static_mesh_path(size_t visible_static_mesh_draw_count)
	{
		return visible_static_mesh_draw_count > 1;
	}

	size_t SceneRenderer::reserve_instance_buffer_slot_range(size_t& next_buffer_slot, size_t slot_count)
	{
		const size_t buffer_slot_base = next_buffer_slot;
		next_buffer_slot += slot_count;
		return buffer_slot_base;
	}

	size_t SceneRenderer::resolve_instance_buffer_slot(size_t buffer_slot_base, size_t local_buffer_index)
	{
		return buffer_slot_base + local_buffer_index;
	}

	size_t SceneRenderer::instance_buffer_frame_lag()
	{
		return k_scene_instance_buffer_frame_lag;
	}

	size_t SceneRenderer::resolve_frame_lagged_instance_buffer_slot(size_t logical_buffer_slot, uint64_t render_frame_index)
	{
		const size_t frame_ring_index =
			static_cast<size_t>(render_frame_index % static_cast<uint64_t>(k_scene_instance_buffer_frame_lag));
		return logical_buffer_slot * k_scene_instance_buffer_frame_lag + frame_ring_index;
	}

	void SceneRenderer::begin_instance_buffer_frame(uint64_t render_frame_index)
	{
		if (m_instance_buffer_frame_index == render_frame_index)
		{
			return;
		}

		m_instance_buffer_frame_index = render_frame_index;
		m_next_instance_buffer_slot = 0;
	}

	uint64_t SceneRenderer::resolve_temporal_view_key(const SceneRenderViewContext& view_context) const
	{
		if (view_context.view_id != 0)
		{
			return view_context.view_id;
		}
		return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view_context.output_target.get()));
	}

	const SceneRenderer::SceneTemporalViewState* SceneRenderer::find_previous_temporal_view_state(
		uint64_t view_key) const
	{
		const auto found = m_temporal_view_states.find(view_key);
		if (found == m_temporal_view_states.end() ||
			!found->second.valid)
		{
			return nullptr;
		}
		return &found->second;
	}

	void SceneRenderer::commit_temporal_view_state(uint64_t view_key, const VisibleRenderFrame& frame)
	{
		if (view_key == 0)
		{
			return;
		}

		SceneTemporalViewState& state = m_temporal_view_states[view_key];
		state.view_projection = frame.view_projection;
		state.jitter_ndc = frame.taa_jitter_ndc;
		state.static_mesh_world_transforms.clear();
		state.static_mesh_world_transforms.reserve(frame.static_mesh_draws.size());
		for (const VisibleStaticMeshDraw& draw : frame.static_mesh_draws)
		{
			const uint64_t temporal_key = resolve_static_mesh_temporal_key(draw);
			if (temporal_key != 0)
			{
				state.static_mesh_world_transforms[temporal_key] = draw.world_transform;
			}
		}
		state.valid = true;
	}

	size_t SceneRenderer::reserve_frame_instance_buffer_slot_range(size_t slot_count)
	{
		return reserve_instance_buffer_slot_range(m_next_instance_buffer_slot, slot_count);
	}

	std::shared_ptr<VertexBuffer> SceneRenderer::ensure_instance_buffer(
		size_t buffer_index,
		const SceneStaticMeshInstanceData* instances,
		uint32_t instance_count)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<VertexBuffer>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer && instances && instance_count > 0);
		const uint32_t instance_buffer_size =
			static_cast<uint32_t>(sizeof(SceneStaticMeshInstanceData) * instance_count);
		if (buffer_index >= m_instance_buffers.size())
		{
			m_instance_buffers.resize(buffer_index + 1);
		}

		SceneInstanceBufferEntry& instance_buffer_entry = m_instance_buffers[buffer_index];
		if (!instance_buffer_entry.buffer || instance_buffer_entry.capacity < instance_count)
		{
			VertexBufferDesc instance_buffer_desc{};
			instance_buffer_desc.size = instance_buffer_size;
			instance_buffer_desc.stride = static_cast<uint32_t>(sizeof(SceneStaticMeshInstanceData));
			instance_buffer_desc.cpu_write = true;
			instance_buffer_desc.initial_data = instances;
			instance_buffer_desc.name = "SceneStaticMeshInstanceBuffer";
			instance_buffer_entry.buffer = m_renderer->create_vertex_buffer(instance_buffer_desc);
			ASH_PROCESS_ERROR(instance_buffer_entry.buffer != nullptr);
			instance_buffer_entry.capacity = instance_count;
		}
		else
		{
			ASH_PROCESS_ERROR(instance_buffer_entry.buffer->update(
				0,
				instance_buffer_size,
				const_cast<SceneStaticMeshInstanceData*>(instances)));
		}

		result = instance_buffer_entry.buffer;
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	bool SceneRenderer::render_visible_frame(VisibleRenderFrame& frame, const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::render_visible_frame", AshEngine::Profile::Color::Scene);
		if (view_context.debug_name)
		{
			ASH_PROFILE_SCOPE_TEXT(view_context.debug_name, std::strlen(view_context.debug_name));
		}
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.static_mesh_draws.size()));
		ASH_PROFILE_PLOT("Scene/StaticMeshDraws", static_cast<int64_t>(frame.static_mesh_draws.size()));
		ASH_PROFILE_PLOT("Scene/Lights", static_cast<int64_t>(frame.lights.size()));
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(validate_view_context(view_context));
		const uint64_t render_frame_index = resolve_render_frame_index(frame);
		begin_instance_buffer_frame(render_frame_index);

		const uint32_t output_width = view_context.output_target->get_width();
		const uint32_t output_height = view_context.output_target->get_height();
		const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();
		ASH_PROFILE_PLOT("Scene/GBufferTargets", static_cast<int64_t>(layout.attachments.size()));
		ASH_PROCESS_ERROR(output_width <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
		ASH_PROCESS_ERROR(output_height <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));

		RenderGraphBuilder graph(*m_renderer, view_context.debug_name ? view_context.debug_name : "SceneRenderGraph");
		RenderGraphTextureRef output = graph.register_external_texture(view_context.output_target, "SceneOutput");
		const uint64_t temporal_view_key = resolve_temporal_view_key(view_context);

		// TAA sub-pixel jitter: perturb the projection so each frame samples a different
		// in-pixel location, then the TAA resolve accumulates them. The previous frame's
		// jitter is needed by the resolve to decouple jitter from the GBuffer motion vectors.
		const TemporalAAConfig& taa_config = frame.render_config.temporal_aa;
		frame.taa_enabled = false;
		frame.taa_jitter_ndc = glm::vec2(0.0f, 0.0f);
		frame.taa_previous_jitter_ndc = glm::vec2(0.0f, 0.0f);
		if (taa_config.enabled)
		{
			const glm::vec2 jitter_ndc = temporal_aa_compute_jitter_ndc(
				frame.frame_index,
				taa_config.jitter_sequence_length,
				output_width,
				output_height);
			if (const SceneTemporalViewState* previous_state = find_previous_temporal_view_state(temporal_view_key))
			{
				frame.taa_previous_jitter_ndc = previous_state->jitter_ndc;
			}
			frame.taa_jitter_ndc = jitter_ndc;
			frame.taa_enabled = true;
			frame.projection[2][0] += jitter_ndc.x;
			frame.projection[2][1] += jitter_ndc.y;
			frame.view_projection = frame.projection * frame.view;
		}

		m_render_debug_view.begin_frame();
		register_render_debug_item(
			m_render_debug_view,
			"SceneOutput",
			"Scene Output",
			output,
			RenderDebugVisualization::Color,
			view_context.output_target->get_format(),
			output_width,
			output_height);

		SceneDeferredGraphResources graph_resources{};
		graph_resources.gbuffer_targets.reserve(layout.attachments.size());
		for (const GBufferAttachmentDesc& attachment : layout.attachments)
		{
			const std::string attachment_name{ attachment.name };
			RenderGraphTextureDesc desc{};
			desc.width = static_cast<uint16_t>(output_width);
			desc.height = static_cast<uint16_t>(output_height);
			desc.format = attachment.format;
			desc.shader_resource = true;
			desc.unordered_access = false;
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = {};
			graph_resources.gbuffer_targets.push_back(graph.create_texture(desc, attachment_name.c_str()));
		}

		constexpr std::array<const char*, 5> k_gbuffer_debug_names = {
			"SceneGBufferA",
			"SceneGBufferB",
			"SceneGBufferC",
			"SceneGBufferD",
			"SceneGBufferE"
		};
		constexpr std::array<const char*, 5> k_gbuffer_debug_display_names = {
			"GBuffer A",
			"GBuffer B",
			"GBuffer C",
			"GBuffer D MotionVector",
			"GBuffer E Normal"
		};
		constexpr std::array<RenderDebugVisualization, 5> k_gbuffer_debug_visualizations = {
			RenderDebugVisualization::Color,
			RenderDebugVisualization::Color,
			RenderDebugVisualization::Color,
			RenderDebugVisualization::MotionVector,
			RenderDebugVisualization::Normal
		};
		for (size_t index = 0; index < graph_resources.gbuffer_targets.size() && index < k_gbuffer_debug_names.size(); ++index)
		{
			register_render_debug_item(
				m_render_debug_view,
				k_gbuffer_debug_names[index],
				k_gbuffer_debug_display_names[index],
				graph_resources.gbuffer_targets[index],
				k_gbuffer_debug_visualizations[index],
				layout.attachments[index].format,
				output_width,
				output_height);
		}

		RenderGraphTextureDesc depth_desc{};
		depth_desc.width = static_cast<uint16_t>(output_width);
		depth_desc.height = static_cast<uint16_t>(output_height);
		depth_desc.format = RenderTextureFormat::D32_SFLOAT;
		depth_desc.shader_resource = true;
		depth_desc.unordered_access = false;
		depth_desc.use_optimized_clear_value = true;
		depth_desc.optimized_clear_depth_stencil = view_context.depth_clear_value;
		graph_resources.depth = graph.create_texture(depth_desc, "SceneDeferredDepth");
		register_render_debug_item(
			m_render_debug_view,
			"SceneDeferredDepth",
			"Depth",
			graph_resources.depth,
			RenderDebugVisualization::Depth,
			depth_desc.format,
			output_width,
			output_height);

		RenderGraphTextureDesc lighting_desc{};
		lighting_desc.width = static_cast<uint16_t>(output_width);
		lighting_desc.height = static_cast<uint16_t>(output_height);
		lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
		lighting_desc.shader_resource = true;
		lighting_desc.unordered_access = false;
		lighting_desc.use_optimized_clear_value = true;
		lighting_desc.optimized_clear_color = RenderColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
		graph_resources.lighting_diffuse = graph.create_texture(lighting_desc, "SceneDeferredLightingDiffuse");
		graph_resources.lighting_specular = graph.create_texture(lighting_desc, "SceneDeferredLightingSpecular");
		graph_resources.scene_hdr_linear = graph.create_texture(lighting_desc, "SceneDeferredSceneHDRLinear");
		register_render_debug_item(
			m_render_debug_view,
			"SceneDeferredLightingDiffuse",
			"Deferred Lighting Diffuse",
			graph_resources.lighting_diffuse,
			RenderDebugVisualization::LinearHDR,
			lighting_desc.format,
			output_width,
			output_height);
		register_render_debug_item(
			m_render_debug_view,
			"SceneDeferredLightingSpecular",
			"Deferred Lighting Specular",
			graph_resources.lighting_specular,
			RenderDebugVisualization::LinearHDR,
			lighting_desc.format,
			output_width,
			output_height);
		register_render_debug_item(
			m_render_debug_view,
			"SceneDeferredSceneHDRLinear",
			"Scene HDR Linear",
			graph_resources.scene_hdr_linear,
			RenderDebugVisualization::LinearHDR,
			lighting_desc.format,
			output_width,
			output_height);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneGBufferPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (uint8_t index = 0; index < static_cast<uint8_t>(graph_resources.gbuffer_targets.size()); ++index)
				{
					pass.write_color(index, graph_resources.gbuffer_targets[index], RenderLoadAction::Clear, {});
				}
				pass.write_depth(graph_resources.depth, RenderLoadAction::Clear, view_context.depth_clear_value);
			},
			[this, &frame, &view_context, render_frame_index](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneGBufferPass", AshEngine::Profile::Color::Draw);
				return render_static_meshes_to_pass(
					frame,
					view_context,
					context,
					render_frame_index,
					PassFamily::GBuffer);
			}));

		// editor begin 修改原因：P2 GPU ID buffer picking
		if (view_context.pick_state != nullptr && view_context.pick_state->request_active)
		{
			ASH_PROCESS_ERROR(add_entity_pick_pass(
				graph,
				graph_resources.depth,
				frame,
				view_context));
		}
		// editor end

		const AmbientOcclusionPassOutputs ao_outputs = m_ambient_occlusion_pass.add_passes(
			graph,
			frame,
			graph_resources,
			view_context,
			frame.render_config.ambient_occlusion);
		graph_resources.ambient_occlusion = ao_outputs.ambient_occlusion;
		ASH_PROCESS_ERROR(graph_resources.ambient_occlusion);
		register_render_debug_item(
			m_render_debug_view,
			"SceneAmbientOcclusionRaw",
			"Ambient Occlusion Raw",
			ao_outputs.raw_ao,
			RenderDebugVisualization::AO,
			RenderTextureFormat::RGBA8_UNORM,
			output_width,
			output_height);
		register_render_debug_item(
			m_render_debug_view,
			"SceneAmbientOcclusion",
			"Ambient Occlusion",
			ao_outputs.final_ao,
			RenderDebugVisualization::AO,
			RenderTextureFormat::RGBA8_UNORM,
			output_width,
			output_height);
		register_render_debug_item(
			m_render_debug_view,
			"SceneAmbientOcclusionTemporal",
			"Ambient Occlusion Temporal",
			ao_outputs.temporal_ao,
			RenderDebugVisualization::AO,
			RenderTextureFormat::RGBA8_UNORM,
			output_width,
			output_height);

		if (ao_outputs.debug_view)
		{
			graph_resources.scene_hdr_linear = ao_outputs.debug_view;
		}
		else
		{
			const DirectionalShadowConfig& directional_shadow_config = frame.render_config.directional_shadows;
			const DirectionalShadowCasterDrawCallback shadow_draw_callback =
				[this](
					const VisibleRenderFrame& shadow_frame,
					const SceneRenderViewContext& shadow_view_context,
					RenderGraphRasterContext& context,
					uint64_t shadow_render_frame_index,
					ShadowCasterMobilityFilter mobility_filter) -> bool
				{
					return render_shadow_static_meshes_to_pass(
						shadow_frame,
						shadow_view_context,
						context,
						shadow_render_frame_index,
						mobility_filter);
				};

			SunLightShadowPassOutputs sunlight_shadow_outputs{};
			if (directional_shadow_config.enabled)
			{
				sunlight_shadow_outputs = m_sunlight_shadow_pass.add_depth_passes(
					graph,
					frame,
					view_context,
					directional_shadow_config,
					render_frame_index,
					shadow_draw_callback);
			}

			graph_resources.sunlight_shadow_dynamic_atlas = sunlight_shadow_outputs.dynamic_atlas;
			graph_resources.sunlight_shadow_static_cache = sunlight_shadow_outputs.static_cache_atlas;
			graph_resources.sunlight_shadow_mask = sunlight_shadow_outputs.shadow_mask;
			graph_resources.sunlight_shadow_cascade_debug = sunlight_shadow_outputs.cascade_debug;

			if (sunlight_shadow_outputs.cascade_debug)
			{
				ASH_PROCESS_ERROR(m_sunlight_shadow_pass.add_cascade_debug_pass(
					graph,
					sunlight_shadow_outputs,
					graph_resources.depth,
					frame,
					view_context));
			}

			if (sunlight_shadow_outputs.dynamic_atlas)
			{
				register_render_debug_item(
					m_render_debug_view,
					"SunLightShadowDynamicAtlas",
					"SunLight Shadow Dynamic Atlas",
					graph_resources.sunlight_shadow_dynamic_atlas,
					RenderDebugVisualization::Depth,
					RenderTextureFormat::D32_SFLOAT,
					directional_shadow_config.dynamic_atlas_size,
					directional_shadow_config.dynamic_atlas_size);
			}
			if (sunlight_shadow_outputs.static_cache_atlas)
			{
				register_render_debug_item(
					m_render_debug_view,
					"SunLightShadowStaticCache",
					"SunLight Shadow Static Cache",
					graph_resources.sunlight_shadow_static_cache,
					RenderDebugVisualization::Depth,
					RenderTextureFormat::D32_SFLOAT,
					directional_shadow_config.static_cache_atlas_size,
					directional_shadow_config.static_cache_atlas_size);
			}
			if (sunlight_shadow_outputs.shadow_mask)
			{
				register_render_debug_item(
					m_render_debug_view,
					"SceneSunLightShadowMask",
					"SunLight Shadow Mask",
					graph_resources.sunlight_shadow_mask,
					RenderDebugVisualization::Scalar,
					RenderTextureFormat::RGBA8_UNORM,
					output_width,
					output_height);
			}
			if (sunlight_shadow_outputs.cascade_debug)
			{
				register_render_debug_item(
					m_render_debug_view,
					"SceneSunLightShadowCascadeIndex",
					"SunLight Shadow Cascade Index",
					graph_resources.sunlight_shadow_cascade_debug,
					RenderDebugVisualization::Color,
					RenderTextureFormat::RGBA8_UNORM,
					output_width,
					output_height);
			}

			ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_base_pass(
				graph,
				frame,
				graph_resources,
				output,
				view_context));

			for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
			{
				const VisibleLightData& light = frame.lights[light_index];
				if (light.type == LightType::Directional)
				{
					RenderGraphTextureRef directional_shadow_mask{};
					if (directional_shadow_config.enabled && light.casts_shadow)
					{
						if (light.sunlight)
						{
							const DirectionalShadowLightPlan* shadow_plan =
								find_shadow_plan_for_frame_light(sunlight_shadow_outputs, light_index);
							if (shadow_plan && shadow_plan->shadowed && sunlight_shadow_outputs.has_shadowed_lights())
							{
								ASH_PROCESS_ERROR(m_sunlight_shadow_pass.add_shadow_mask_pass(
									graph,
									sunlight_shadow_outputs,
									shadow_plan->light_plan_index,
									frame,
									graph_resources,
									view_context));
								directional_shadow_mask = sunlight_shadow_outputs.shadow_mask;
							}
						}
						else
						{
							DirectionalLightShadowPassOutputs ordinary_outputs =
								m_directional_light_shadow_pass.add_shadow_passes(
									graph,
									frame,
									light_index,
									view_context,
									directional_shadow_config,
									render_frame_index,
									shadow_draw_callback);
							ASH_PROCESS_ERROR(ordinary_outputs.has_shadow());
							ASH_PROCESS_ERROR(m_directional_light_shadow_pass.add_shadow_mask_pass(
								graph,
								ordinary_outputs,
								frame,
								graph_resources,
								view_context));
							graph_resources.directional_light_shadow_transient_atlas = ordinary_outputs.dynamic_atlas;
							graph_resources.directional_light_shadow_transient_mask = ordinary_outputs.shadow_mask;
							directional_shadow_mask = ordinary_outputs.shadow_mask;
						}
					}
					ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_directional_light_pass(
						graph,
						frame,
						graph_resources,
						output,
						view_context,
						light_index,
						directional_shadow_mask));
				}
				else if (light.type == LightType::Point)
				{
					ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_point_light_pass(
						graph,
						frame,
						graph_resources,
						output,
						view_context,
						light_index));
				}
				else if (light.type == LightType::Spot)
				{
					ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_spot_light_pass(
						graph,
						frame,
						graph_resources,
						output,
						view_context,
						light_index));
				}
			}

			if (graph_resources.directional_light_shadow_transient_atlas)
			{
				register_render_debug_item(
					m_render_debug_view,
					"DirectionalLightShadowTransientAtlas",
					"Directional Light Shadow Transient Atlas",
					graph_resources.directional_light_shadow_transient_atlas,
					RenderDebugVisualization::Depth,
					RenderTextureFormat::D32_SFLOAT,
					directional_shadow_config.dynamic_atlas_size,
					directional_shadow_config.dynamic_atlas_size);
			}
			if (graph_resources.directional_light_shadow_transient_mask)
			{
				register_render_debug_item(
					m_render_debug_view,
					"DirectionalLightShadowTransientMask",
					"Directional Light Shadow Transient Mask",
					graph_resources.directional_light_shadow_transient_mask,
					RenderDebugVisualization::Scalar,
					RenderTextureFormat::RGBA8_UNORM,
					output_width,
					output_height);
			}

			ASH_PROCESS_ERROR(m_environment_lighting_pass.add_pass(
				graph,
				frame,
				graph_resources,
				view_context));
			ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_composite_pass(
				graph,
				frame,
				graph_resources,
				output,
				view_context));
			ASH_PROCESS_ERROR(m_sky_background_pass.add_pass(
				graph,
				frame,
				graph_resources.depth,
				graph_resources.scene_hdr_linear,
				view_context));
			const VolumetricLightingPassOutputs volumetric_outputs = m_volumetric_lighting_pass.add_passes(
				graph,
				frame,
				graph_resources,
				graph_resources.scene_hdr_linear,
				view_context,
				frame.render_config.volumetric_lighting,
				&sunlight_shadow_outputs);
			ASH_PROCESS_ERROR(volumetric_outputs.scene_hdr_linear);
			graph_resources.scene_hdr_linear = volumetric_outputs.scene_hdr_linear;
			graph_resources.volumetric_density = volumetric_outputs.density;
			graph_resources.volumetric_scattering = volumetric_outputs.scattering;
			graph_resources.volumetric_integrated_lighting = volumetric_outputs.integrated_lighting;
			graph_resources.volumetric_history_validity = volumetric_outputs.history_validity;
			graph_resources.volumetric_composite_hdr = volumetric_outputs.composite_hdr;
			graph_resources.lightshaft_screen_space_mask = volumetric_outputs.screen_space_mask;
			graph_resources.lightshaft_screen_space_final = volumetric_outputs.screen_space_final;
			const uint32_t volumetric_atlas_width = volumetric_outputs.atlas_width != 0u ? volumetric_outputs.atlas_width : output_width;
			const uint32_t volumetric_atlas_height = volumetric_outputs.atlas_height != 0u ? volumetric_outputs.atlas_height : output_height;
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricDensity",
				"Volumetric Density",
				volumetric_outputs.density,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA32_SFLOAT,
				volumetric_atlas_width,
				volumetric_atlas_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricScattering",
				"Volumetric Scattering",
				volumetric_outputs.scattering,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA32_SFLOAT,
				volumetric_atlas_width,
				volumetric_atlas_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricIntegratedLighting",
				"Volumetric Integrated Lighting",
				volumetric_outputs.integrated_lighting,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA32_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricCompositeHDR",
				"Volumetric Composite HDR",
				volumetric_outputs.composite_hdr,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneVolumetricHistoryValidity",
				"Volumetric History Validity",
				volumetric_outputs.history_validity,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA32_SFLOAT,
				volumetric_atlas_width,
				volumetric_atlas_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneLightShaftOcclusionMask",
				"LightShaft Screen Space Mask",
				volumetric_outputs.screen_space_mask,
				RenderDebugVisualization::Scalar,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneLightShaftScreenSpaceFinal",
				"LightShaft Screen Space Final",
				volumetric_outputs.screen_space_final,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			const BloomPassOutputs bloom_outputs = m_bloom_pass.add_passes(
				graph,
				frame,
				graph_resources.scene_hdr_linear,
				view_context,
				frame.render_config.bloom);
			ASH_PROCESS_ERROR(bloom_outputs.scene_hdr_linear);
			graph_resources.scene_hdr_linear = bloom_outputs.scene_hdr_linear;

			const TemporalAAPassOutputs taa_outputs = m_taa_pass.add_passes(
				graph,
				frame,
				graph_resources,
				graph_resources.scene_hdr_linear,
				view_context,
				frame.render_config.temporal_aa);
			ASH_PROCESS_ERROR(taa_outputs.scene_hdr_linear);
			graph_resources.scene_hdr_linear = taa_outputs.scene_hdr_linear;
			if (taa_outputs.resolved.is_valid())
			{
				register_render_debug_item(
					m_render_debug_view,
					"SceneTemporalAAResolved",
					"Temporal AA Resolved",
					taa_outputs.resolved,
					RenderDebugVisualization::LinearHDR,
					RenderTextureFormat::RGBA16_SFLOAT,
					output_width,
					output_height);
			}

			register_render_debug_item(
				m_render_debug_view,
				"SceneBloomSetup",
				"Bloom Setup",
				bloom_outputs.setup,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			for (uint32_t bloom_mip_index = 0; bloom_mip_index < static_cast<uint32_t>(bloom_outputs.mips.size()); ++bloom_mip_index)
			{
				const RenderGraphTextureRef bloom_mip = bloom_outputs.mips[bloom_mip_index];
				const std::string debug_name = "SceneBloomMip" + std::to_string(bloom_mip_index + 1u);
				const std::string display_name = "Bloom Mip " + std::to_string(bloom_mip_index + 1u);
				register_render_debug_item(
					m_render_debug_view,
					debug_name.c_str(),
					display_name.c_str(),
					bloom_mip,
					RenderDebugVisualization::LinearHDR,
					RenderTextureFormat::RGBA16_SFLOAT,
					std::max<uint32_t>(output_width >> (bloom_mip_index + 1u), 1u),
					std::max<uint32_t>(output_height >> (bloom_mip_index + 1u), 1u));
			}
			register_render_debug_item(
				m_render_debug_view,
				"SceneBloomFinal",
				"Bloom Final",
				bloom_outputs.final_bloom,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
			register_render_debug_item(
				m_render_debug_view,
				"SceneBloomCompositeHDR",
				"Bloom Composite HDR",
				bloom_outputs.composite_hdr,
				RenderDebugVisualization::LinearHDR,
				RenderTextureFormat::RGBA16_SFLOAT,
				output_width,
				output_height);
		}
		ASH_PROCESS_ERROR(m_post_process_tone_map_pass.add_pass(
			graph,
			frame,
			graph_resources.scene_hdr_linear,
			output,
			view_context));
		ASH_PROCESS_ERROR(m_render_debug_view.add_pass(graph, output, view_context));
		ASH_PROCESS_ERROR(add_scene_view_overlay_pass(
			graph,
			output,
			graph_resources.depth,
			frame,
			view_context));
		ASH_PROCESS_ERROR(add_debug_draw_overlay_pass(graph, output, frame, view_context));
		ASH_PROCESS_ERROR(graph.execute());

		// editor begin 修改原因：P2 GPU ID buffer picking
		if (view_context.pick_state != nullptr &&
			view_context.pick_state->request_active &&
			m_entity_pick_target != nullptr &&
			m_renderer != nullptr)
		{
			RenderDevice* render_device = m_renderer->get_render_device();
			if (render_device != nullptr)
			{
				RenderDevice::RenderTextureTexelReadDesc read_desc{};
				read_desc.x = view_context.pick_state->request_x;
				read_desc.y = view_context.pick_state->request_y;
				if (render_device->queue_render_target_texel_read(m_entity_pick_target, read_desc))
				{
					m_pending_pick_readback.active = true;
					m_pending_pick_readback.pick_state = view_context.pick_state;
					m_pending_pick_readback.scene = view_context.scene;
					m_pending_pick_readback.x = read_desc.x;
					m_pending_pick_readback.y = read_desc.y;
					m_pending_pick_readback.width = output_width;
					m_pending_pick_readback.height = output_height;
					m_pending_pick_readback.frame = frame;
				}
			}
		}
		// editor end

		commit_temporal_view_state(temporal_view_key, frame);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SceneRenderer::draw_render_debug_view_ui(UIContext& ui_context)
	{
		m_render_debug_view.draw_ui(ui_context);
	}

	bool SceneRenderer::add_scene_view_overlay_pass(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef output_target,
		RenderGraphTextureRef depth_target,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::add_scene_view_overlay_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!view_context.scene_overlay_lines || view_context.scene_overlay_lines->empty())
		{
			return true;
		}

		auto overlay_batches = std::make_shared<std::vector<SceneOverlayDrawBatch>>(
			build_scene_overlay_draw_batches(*view_context.scene_overlay_lines));
		if (overlay_batches->empty())
		{
			return true;
		}

		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(view_context.scene_overlay_lines->size()));
		ASH_PROFILE_PLOT("Scene/OverlayLines", static_cast<int64_t>(view_context.scene_overlay_lines->size()));
		ASH_PROCESS_ERROR(output_target.is_valid());
		ASH_PROCESS_ERROR(m_debug_draw_program != nullptr);
		ASH_PROCESS_ERROR(m_scene_overlay_depth_test_program != nullptr);
		ASH_PROCESS_ERROR(m_scene_overlay_depth_test_no_write_program != nullptr);

		auto draw_overlay_batches =
			[this, overlay_batches, &frame, &view_context](
				RenderGraphRasterContext& context,
				bool depth_tested) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, pass_result, true, false);
			for (const SceneOverlayDrawBatch& batch : *overlay_batches)
			{
				const bool batch_depth_tested =
					batch.depth_mode == SceneOverlayDepthMode::DepthTest ||
					batch.depth_mode == SceneOverlayDepthMode::DepthTestNoWrite;
				if (batch_depth_tested != depth_tested)
				{
					continue;
				}

				GraphicsProgram* program = m_debug_draw_program.get();
				if (batch_depth_tested)
				{
					program = m_scene_overlay_depth_test_no_write_program.get();
				}
				ASH_PROCESS_ERROR(program != nullptr);

				ASH_PROCESS_ERROR(ensure_debug_draw_vertex_buffer(
					*m_renderer,
					m_scene_overlay_vertex_buffer,
					m_scene_overlay_vertex_capacity,
					batch.vertices));

				GraphicsDrawDesc draw_desc{};
				draw_desc.program = program;
				draw_desc.vertex_buffers.push_back({ 0u, m_scene_overlay_vertex_buffer, 0u });
				draw_desc.vertex_count = static_cast<uint32_t>(batch.vertices.size());
				draw_desc.instance_count = 1u;

				DebugDrawRootConstants constants{};
				constants.view_projection = frame.view_projection;
				constants.depth_bias = batch.depth_bias;
				if (view_context.reverse_z)
				{
					constants.depth_bias = -constants.depth_bias;
				}
				attach_debug_draw_root_constants(draw_desc, program, constants);
				apply_view_context_to_draw_desc(draw_desc, view_context);
				ASH_PROCESS_ERROR(context.draw(draw_desc));
			}

			ASH_PROCESS_GUARD_RETURN_END(pass_result, false);
		};

		bool has_depth_tested_batches = false;
		bool has_always_on_top_batches = false;
		for (const SceneOverlayDrawBatch& batch : *overlay_batches)
		{
			if (batch.depth_mode == SceneOverlayDepthMode::DepthTest ||
				batch.depth_mode == SceneOverlayDepthMode::DepthTestNoWrite)
			{
				has_depth_tested_batches = true;
			}
			if (batch.depth_mode == SceneOverlayDepthMode::AlwaysOnTop)
			{
				has_always_on_top_batches = true;
			}
		}

		if (has_depth_tested_batches)
		{
			ASH_PROCESS_ERROR(depth_target.is_valid());
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneViewOverlayDepthPass",
				RenderGraphPassFlags::None,
				[output_target, depth_target](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_depth(depth_target, RenderGraphDepthReadMode::DepthTestOnly);
					pass.write_color(0, output_target, RenderLoadAction::Load, {});
				},
				[this, overlay_batches, draw_overlay_batches, &frame, &view_context](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneViewOverlayDepthPass", AshEngine::Profile::Color::Draw);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(overlay_batches->size()));
					return draw_overlay_batches(context, true);
				}));
		}

		if (has_always_on_top_batches)
		{
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneViewOverlayTopPass",
				RenderGraphPassFlags::None,
				[output_target](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_color(0, output_target, RenderLoadAction::Load, {});
				},
				[this, overlay_batches, draw_overlay_batches, &frame, &view_context](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneViewOverlayTopPass", AshEngine::Profile::Color::Draw);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(overlay_batches->size()));
					return draw_overlay_batches(context, false);
				}));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::add_debug_draw_overlay_pass(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef output_target,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::add_debug_draw_overlay_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!m_debug_draw_service)
		{
			return true;
		}

		auto debug_lines = std::make_shared<std::vector<DebugDrawLine>>();
		m_debug_draw_service->snapshot_lines(*debug_lines);
		if (debug_lines->empty())
		{
			return true;
		}

		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(debug_lines->size()));
		ASH_PROFILE_PLOT("Scene/DebugDrawLines", static_cast<int64_t>(debug_lines->size()));
		ASH_PROCESS_ERROR(m_debug_draw_program != nullptr);
		ASH_PROCESS_ERROR(output_target);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDebugDrawOverlayPass",
			RenderGraphPassFlags::None,
			[output_target](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(output_target, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, output_target, RenderLoadAction::Load, {});
			},
			[this, debug_lines, &frame, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDebugDrawOverlayPass", AshEngine::Profile::Color::Draw);
				ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(debug_lines->size()));
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				ASH_PROCESS_ERROR(m_renderer != nullptr);
				ASH_PROCESS_ERROR(m_debug_draw_program != nullptr);
				ASH_PROCESS_ERROR(debug_lines && !debug_lines->empty());
				ASH_PROCESS_ERROR(debug_lines->size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max() / 2u));

				const std::vector<DebugDrawVertex> vertices = make_debug_draw_vertices(*debug_lines);
				ASH_PROCESS_ERROR(ensure_debug_draw_vertex_buffer(
					*m_renderer,
					m_debug_draw_vertex_buffer,
					m_debug_draw_vertex_capacity,
					vertices));

				GraphicsDrawDesc draw_desc{};
				draw_desc.program = m_debug_draw_program.get();
				draw_desc.vertex_buffers.push_back({ 0u, m_debug_draw_vertex_buffer, 0u });
				draw_desc.vertex_count = static_cast<uint32_t>(vertices.size());
				draw_desc.instance_count = 1u;
				DebugDrawRootConstants constants{};
				constants.view_projection = frame.view_projection;
				attach_debug_draw_root_constants(draw_desc, m_debug_draw_program.get(), constants);
				apply_view_context_to_draw_desc(draw_desc, view_context);
				ASH_PROCESS_ERROR(context.draw(draw_desc));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::render_shadow_static_meshes_to_pass(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& pass_context,
		uint64_t render_frame_index,
		ShadowCasterMobilityFilter mobility_filter)
	{
		VisibleRenderFrame shadow_frame = frame;
		shadow_frame.static_mesh_draws.clear();
		for (const VisibleStaticMeshDraw& draw : frame.shadow_caster_static_mesh_draws)
		{
			const bool static_match = draw.mobility == SceneMobility::Static || draw.mobility == SceneMobility::Stationary;
			const bool dynamic_match = draw.mobility == SceneMobility::Movable;
			if (mobility_filter == ShadowCasterMobilityFilter::All ||
				(mobility_filter == ShadowCasterMobilityFilter::StaticOnly && static_match) ||
				(mobility_filter == ShadowCasterMobilityFilter::DynamicOnly && dynamic_match))
			{
				shadow_frame.static_mesh_draws.push_back(draw);
			}
		}
		return render_static_meshes_to_pass(
			shadow_frame,
			view_context,
			pass_context,
			render_frame_index,
			PassFamily::DepthOnly);
	}

	bool SceneRenderer::render_static_meshes_to_pass(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& pass_context,
		uint64_t render_frame_index,
		PassFamily pass_family)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::render_static_meshes_to_pass", AshEngine::Profile::Color::Draw);
		const char* pass_label = get_staticmesh_pass_label(pass_family);
		ASH_PROFILE_SCOPE_TEXT(pass_label, std::strlen(pass_label));
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.static_mesh_draws.size()));
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const bool use_instanced_static_mesh_path =
			should_use_instanced_static_mesh_path(frame.static_mesh_draws.size());
		const SceneTemporalViewState* previous_temporal_state = nullptr;
		if (should_use_temporal_history_for_pass(pass_family))
		{
			const uint64_t temporal_view_key = resolve_temporal_view_key(view_context);
			previous_temporal_state = find_previous_temporal_view_state(temporal_view_key);
		}
		const auto build_instance_data = [&frame, previous_temporal_state](const VisibleStaticMeshDraw& draw) -> SceneStaticMeshInstanceData
		{
			glm::mat4 previous_world_transform = draw.world_transform;
			glm::mat4 previous_view_projection = frame.view_projection;
			bool temporal_valid = false;
			if (previous_temporal_state)
			{
				const uint64_t temporal_key = resolve_static_mesh_temporal_key(draw);
				const auto found_previous_transform =
					previous_temporal_state->static_mesh_world_transforms.find(temporal_key);
				if (found_previous_transform != previous_temporal_state->static_mesh_world_transforms.end())
				{
					previous_world_transform = found_previous_transform->second;
					previous_view_projection = previous_temporal_state->view_projection;
					temporal_valid = true;
				}
			}
			return make_instance_data(
				frame.view_projection * draw.world_transform,
				previous_view_projection * previous_world_transform,
				temporal_valid);
		};
		if (!use_instanced_static_mesh_path)
		{
			if (!frame.static_mesh_draws.empty())
			{
				const VisibleStaticMeshDraw& draw = frame.static_mesh_draws.front();
				ASH_PROCESS_ERROR(validate_static_mesh_draw_asset(draw));
				const SceneStaticMeshInstanceData instance_data = build_instance_data(draw);
				const size_t logical_instance_buffer_slot = reserve_frame_instance_buffer_slot_range(1);
				const size_t instance_buffer_slot =
					resolve_frame_lagged_instance_buffer_slot(logical_instance_buffer_slot, render_frame_index);
				std::shared_ptr<VertexBuffer> instance_buffer =
					ensure_instance_buffer(instance_buffer_slot, &instance_data, 1);
				ASH_PROCESS_ERROR(instance_buffer != nullptr);

				for (const ResolvedStaticMeshSection& section : draw.sections)
				{
					ASH_PROCESS_ERROR(section.topology == MeshPrimitiveTopology::Triangles);
					if (!section.material || !section.material_proxy)
					{
						HLogError("SceneRenderer: skipping static mesh section with incomplete material bindings.");
						continue;
					}

					const MaterialResource* material_resource =
						select_staticmesh_material_resource(*section.material_proxy, pass_family);
					if (!material_resource)
					{
						HLogError(
							"SceneRenderer: skipping material '{}' because no '{}' render resource is available.",
							section.material->get_asset_path().generic_string(),
							get_staticmesh_pass_label(pass_family));
						continue;
					}

					if (!supports_staticmesh_pass(*material_resource, pass_family))
					{
						continue;
					}
					if (material_resource->pass_relevance.is_transparent)
					{
						const std::string material_key =
							section.material->get_asset_path().empty() ?
							section.material->get_name() :
							section.material->get_asset_path().generic_string();
						log_warning_once(
							"transparent#" + material_key,
							"SceneRenderer: skipping transparent material '" +
							material_key +
							"' in " +
							get_staticmesh_pass_label(pass_family) +
							".");
						continue;
					}
					GraphicsProgram* program = material_resource->program;
					if (!program)
					{
						HLogError(
							"SceneRenderer: skipping material '{}' because its graphics program is unavailable.",
							section.material->get_asset_path().generic_string());
						continue;
					}

					log_staticmesh_pass_usage_once(*section.material, *material_resource, pass_family);

					GraphicsDrawDesc draw_desc{};
					draw_desc.program = program;
					draw_desc.vertex_buffers.push_back({
						0,
						draw.render_asset->resource->vertex_buffer,
						0
					});
					draw_desc.vertex_buffers.push_back({
						1,
						instance_buffer,
						0
					});
					draw_desc.index_buffer = draw.render_asset->resource->index_buffer;
					draw_desc.first_index = section.first_index;
					draw_desc.index_count = section.index_count;
					draw_desc.instance_count = 1;
					draw_desc.vertex_offset = 0;
					apply_view_context_to_draw_desc(draw_desc, view_context);
					ASH_PROCESS_ERROR(pass_context.draw(draw_desc));
				}
			}

			ASH_PROFILE_PLOT("Scene/StaticMeshBatches", static_cast<int64_t>(frame.static_mesh_draws.size()));
		}
		else
		{
			ASH_PROFILE_SCOPE_NC("SceneRenderer::BuildStaticMeshBatches", AshEngine::Profile::Color::Submit);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.static_mesh_draws.size()));
			static thread_local std::vector<StaticMeshDrawBatch> batches{};
			static thread_local std::unordered_map<StaticMeshDrawBatchKey, size_t, StaticMeshDrawBatchKeyHash> batch_lookup{};
			size_t active_batch_count = 0;
			struct BatchScratchRelease
			{
				std::vector<StaticMeshDrawBatch>& batches;
				size_t& active_batch_count;
				~BatchScratchRelease()
				{
					clear_static_mesh_batch_resource_refs(batches, active_batch_count);
				}
			} release_batch_scratch{ batches, active_batch_count };
			batches.reserve(frame.static_mesh_draws.size());
			batch_lookup.clear();
			batch_lookup.reserve(frame.static_mesh_draws.size());
			for (const VisibleStaticMeshDraw& draw : frame.static_mesh_draws)
			{
				ASH_PROCESS_ERROR(validate_static_mesh_draw_asset(draw));
				for (const ResolvedStaticMeshSection& section : draw.sections)
				{
					ASH_PROCESS_ERROR(section.topology == MeshPrimitiveTopology::Triangles);
					if (!section.material || !section.material_proxy)
					{
						HLogError("SceneRenderer: skipping static mesh section with incomplete material bindings.");
						continue;
					}

					const MaterialResource* material_resource =
						select_staticmesh_material_resource(*section.material_proxy, pass_family);
					if (!material_resource)
					{
						HLogError(
							"SceneRenderer: skipping material '{}' because no '{}' render resource is available.",
							section.material->get_asset_path().generic_string(),
							get_staticmesh_pass_label(pass_family));
						continue;
					}

					if (!supports_staticmesh_pass(*material_resource, pass_family))
					{
						continue;
					}
					if (material_resource->pass_relevance.is_transparent)
					{
						const std::string material_key =
							section.material->get_asset_path().empty() ?
							section.material->get_name() :
							section.material->get_asset_path().generic_string();
						log_warning_once(
							"transparent#" + material_key,
							"SceneRenderer: skipping transparent material '" +
							material_key +
							"' in " +
							get_staticmesh_pass_label(pass_family) +
							".");
						continue;
					}
					GraphicsProgram* program = material_resource->program;
					if (!program)
					{
						HLogError(
							"SceneRenderer: skipping material '{}' because its graphics program is unavailable.",
							section.material->get_asset_path().generic_string());
						continue;
					}

					log_staticmesh_pass_usage_once(*section.material, *material_resource, pass_family);

					StaticMeshDrawBatch& batch = find_or_add_batch(batches, active_batch_count, batch_lookup, program, draw.render_asset, section);
					batch.instances.push_back(build_instance_data(draw));
				}
			}

			ASH_PROFILE_PLOT("Scene/StaticMeshBatches", static_cast<int64_t>(active_batch_count));
			const size_t logical_instance_buffer_slot_base =
				reserve_frame_instance_buffer_slot_range(active_batch_count);
			for (size_t batch_index = 0; batch_index < active_batch_count; ++batch_index)
			{
				StaticMeshDrawBatch& batch = batches[batch_index];
				ASH_PROCESS_ERROR(batch.program && batch.vertex_buffer && batch.index_buffer && !batch.instances.empty());
				const uint32_t instance_count = static_cast<uint32_t>(batch.instances.size());
				const size_t logical_instance_buffer_slot =
					resolve_instance_buffer_slot(logical_instance_buffer_slot_base, batch_index);
				const size_t instance_buffer_slot =
					resolve_frame_lagged_instance_buffer_slot(logical_instance_buffer_slot, render_frame_index);
				std::shared_ptr<VertexBuffer> instance_buffer =
					ensure_instance_buffer(
						instance_buffer_slot,
						batch.instances.data(),
						instance_count);
				ASH_PROCESS_ERROR(instance_buffer != nullptr);

				GraphicsDrawDesc draw_desc{};
				draw_desc.program = batch.program;
				draw_desc.vertex_buffers.push_back({
					0,
					batch.vertex_buffer,
					0
				});
				draw_desc.vertex_buffers.push_back({
					1,
					instance_buffer,
					0
				});
				draw_desc.index_buffer = batch.index_buffer;
				draw_desc.first_index = batch.first_index;
				draw_desc.index_count = batch.index_count;
				draw_desc.instance_count = instance_count;
				draw_desc.vertex_offset = 0;
				apply_view_context_to_draw_desc(draw_desc, view_context);
				ASH_PROCESS_ERROR(pass_context.draw(draw_desc));
			}
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	// editor begin 修改原因：P2 GPU ID buffer picking
	bool SceneRenderer::ensure_entity_pick_target(uint32_t width, uint32_t height)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(width > 0 && height > 0);
		if (m_entity_pick_target &&
			m_entity_pick_width == width &&
			m_entity_pick_height == height)
		{
			return true;
		}

		RenderTargetDesc desc{};
		desc.width = static_cast<uint16_t>(width);
		desc.height = static_cast<uint16_t>(height);
		desc.format = RenderTextureFormat::R32G32_UINT;
		desc.shader_resource = true;
		desc.unordered_access = false;
		desc.name = "SceneEntityPick";
		desc.use_optimized_clear_value = true;
		desc.optimized_clear_color = {};
		m_entity_pick_target = m_renderer->create_render_target(desc);
		ASH_PROCESS_ERROR(m_entity_pick_target != nullptr);
		m_entity_pick_width = width;
		m_entity_pick_height = height;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::render_entity_pick_meshes(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& pass_context,
		uint64_t render_frame_index)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::render_entity_pick_meshes", AshEngine::Profile::Color::Draw);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_entity_pick_program != nullptr);

		const auto build_instance_data = [&frame](const VisibleStaticMeshDraw& draw) -> SceneStaticMeshInstanceData
		{
			return make_instance_data(
				frame.view_projection * draw.world_transform,
				frame.view_projection * draw.world_transform,
				false);
		};

		for (const VisibleStaticMeshDraw& draw : frame.static_mesh_draws)
		{
			if (draw.entity_id == 0)
			{
				continue;
			}

			ASH_PROCESS_ERROR(validate_static_mesh_draw_asset(draw));
			const SceneStaticMeshInstanceData instance_data = build_instance_data(draw);
			const size_t logical_instance_buffer_slot = reserve_frame_instance_buffer_slot_range(1);
			const size_t instance_buffer_slot =
				resolve_frame_lagged_instance_buffer_slot(logical_instance_buffer_slot, render_frame_index);
			std::shared_ptr<VertexBuffer> instance_buffer =
				ensure_instance_buffer(instance_buffer_slot, &instance_data, 1);
			ASH_PROCESS_ERROR(instance_buffer != nullptr);

			SceneEntityPickRootConstants constants{};
			const glm::uvec2 packed_entity_id = pack_entity_id(draw.entity_id);
			constants.entity_id = packed_entity_id;

			for (const ResolvedStaticMeshSection& section : draw.sections)
			{
				ASH_PROCESS_ERROR(section.topology == MeshPrimitiveTopology::Triangles);
				if (!section.material || !section.material_proxy)
				{
					continue;
				}

				const MaterialResource* material_resource =
					select_staticmesh_material_resource(*section.material_proxy, PassFamily::DepthOnly);
				if (!material_resource || material_resource->pass_relevance.is_transparent)
				{
					continue;
				}

				GraphicsDrawDesc draw_desc{};
				draw_desc.program = m_entity_pick_program.get();
				draw_desc.vertex_buffers.push_back({ 0, draw.render_asset->resource->vertex_buffer, 0 });
				draw_desc.vertex_buffers.push_back({ 1, instance_buffer, 0 });
				draw_desc.index_buffer = draw.render_asset->resource->index_buffer;
				draw_desc.first_index = section.first_index;
				draw_desc.index_count = section.index_count;
				draw_desc.instance_count = 1;
				draw_desc.vertex_offset = 0;
				attach_entity_pick_root_constants(draw_desc, m_entity_pick_program.get(), constants);
				apply_view_context_to_draw_desc(draw_desc, view_context);
				ASH_PROCESS_ERROR(pass_context.draw(draw_desc));
			}
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::add_entity_pick_pass(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef depth_target,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::add_entity_pick_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(depth_target.is_valid());
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);
		ASH_PROCESS_ERROR(ensure_entity_pick_target(
			view_context.output_target->get_width(),
			view_context.output_target->get_height()));

		RenderGraphTextureRef entity_pick =
			graph.register_external_texture(m_entity_pick_target, "SceneEntityPick");
		const uint64_t render_frame_index = resolve_render_frame_index(frame);
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneEntityPickPass",
			RenderGraphPassFlags::None,
			[entity_pick, depth_target](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_depth(depth_target, RenderGraphDepthReadMode::DepthTestOnly);
				pass.write_color(0, entity_pick, RenderLoadAction::Clear, {});
			},
			[this, &frame, &view_context, render_frame_index](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneEntityPickPass", AshEngine::Profile::Color::Draw);
				return render_entity_pick_meshes(frame, view_context, context, render_frame_index);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SceneRenderer::complete_pending_pick_readbacks(
		Renderer& renderer,
		RenderAssetManager& render_asset_manager)
	{
		if (!m_pending_pick_readback.active || m_pending_pick_readback.pick_state == nullptr)
		{
			return;
		}

		RenderDevice* render_device = renderer.get_render_device();
		if (render_device == nullptr)
		{
			m_pending_pick_readback.active = false;
			return;
		}

		uint32_t packed_entity_id[2] = { 0u, 0u };
		if (!render_device->flush_queued_render_target_texel_reads(packed_entity_id, sizeof(packed_entity_id)))
		{
			m_pending_pick_readback.active = false;
			return;
		}

		ScenePickFrameState& pick_state = *m_pending_pick_readback.pick_state;
		pick_state.result = {};
		pick_state.result.entity_id =
			static_cast<EntityId>(packed_entity_id[0]) |
			(static_cast<EntityId>(packed_entity_id[1]) << 32);

		AssetDatabase* asset_database = render_asset_manager.get_asset_database();
		if (pick_state.result.entity_id != 0)
		{
			pick_state.result.hit = true;
			if (m_pending_pick_readback.scene != nullptr && asset_database != nullptr)
			{
				const SceneRay ray = screen_to_world_ray(
					static_cast<float>(m_pending_pick_readback.x) + 0.5f,
					static_cast<float>(m_pending_pick_readback.y) + 0.5f,
					static_cast<float>(m_pending_pick_readback.width),
					static_cast<float>(m_pending_pick_readback.height),
					m_pending_pick_readback.frame.view,
					m_pending_pick_readback.frame.projection);
				const std::vector<SceneRayHit> hits =
					ray_cast_scene(*m_pending_pick_readback.scene, *asset_database, ray);
				for (const SceneRayHit& hit : hits)
				{
					if (hit.entity_id != pick_state.result.entity_id)
					{
						continue;
					}

					pick_state.result.world_position = hit.position;
					pick_state.result.depth = hit.distance;
					if (hit.bounds.is_valid)
					{
						const glm::vec3 local = hit.position - hit.bounds.center;
						const float length = glm::length(local);
						if (length > 1e-5f)
						{
							pick_state.result.world_normal = local / length;
						}
					}
					break;
				}
			}
		}

		pick_state.result_ready = true;
		m_pending_pick_readback.active = false;
	}
	// editor end

	bool SceneRenderer::validate_view_context(const SceneRenderViewContext& view_context) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);
		const uint32_t output_width = view_context.output_target->get_width();
		const uint32_t output_height = view_context.output_target->get_height();
		ASH_PROCESS_ERROR(output_width > 0);
		ASH_PROCESS_ERROR(output_height > 0);

		if (view_context.has_viewport)
		{
			ASH_PROCESS_ERROR(view_context.viewport.width > 0);
			ASH_PROCESS_ERROR(view_context.viewport.height > 0);
			ASH_PROCESS_ERROR(view_context.viewport.x >= 0);
			ASH_PROCESS_ERROR(view_context.viewport.y >= 0);
			const uint32_t viewport_right = static_cast<uint32_t>(view_context.viewport.x) + static_cast<uint32_t>(view_context.viewport.width);
			const uint32_t viewport_bottom = static_cast<uint32_t>(view_context.viewport.y) + static_cast<uint32_t>(view_context.viewport.height);
			ASH_PROCESS_ERROR(viewport_right <= output_width);
			ASH_PROCESS_ERROR(viewport_bottom <= output_height);
		}

		if (view_context.has_scissor)
		{
			ASH_PROCESS_ERROR(view_context.scissor.width > 0);
			ASH_PROCESS_ERROR(view_context.scissor.height > 0);
			ASH_PROCESS_ERROR(view_context.scissor.x >= 0);
			ASH_PROCESS_ERROR(view_context.scissor.y >= 0);
			const uint32_t scissor_right = static_cast<uint32_t>(view_context.scissor.x) + static_cast<uint32_t>(view_context.scissor.width);
			const uint32_t scissor_bottom = static_cast<uint32_t>(view_context.scissor.y) + static_cast<uint32_t>(view_context.scissor.height);
			ASH_PROCESS_ERROR(scissor_right <= output_width);
			ASH_PROCESS_ERROR(scissor_bottom <= output_height);
		}

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError("SceneRenderer: invalid view context for '{}'.", view_context.debug_name ? view_context.debug_name : "SceneRenderGraph");
		}
		return bResult;
	}

	void SceneRenderer::log_warning_once(const std::string& key, const std::string& message)
	{
		if (!m_logged_warning_keys.insert(key).second)
		{
			return;
		}
		HLogWarning("{}", message);
	}

	void SceneRenderer::log_staticmesh_pass_usage_once(
		const MaterialInterface& material,
		const MaterialResource& resource,
		PassFamily pass_family)
	{
		const std::string material_label = build_material_label(material);
		const std::string log_key =
			material_label +
			"#" +
			get_staticmesh_pass_label(pass_family) +
			"#" +
			std::to_string(resource.combined_source_hash);
		if (!m_logged_material_usage_keys.insert(log_key).second)
		{
			return;
		}

		HLogInfo(
			"SceneRenderer: drawing {} with V2 material '{}' "
			"(program='{}', source_hash={}).",
			get_staticmesh_pass_label(pass_family),
			material_label,
			resource.program_name.empty() ? std::string("<unnamed-program>") : resource.program_name,
			resource.combined_source_hash);
	}
}
