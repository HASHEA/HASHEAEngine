#include "Function/Render/SceneRenderer.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/GBufferLayout.h"
#include "Function/Render/MaterialRenderProxy.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/VertexLayoutPresets.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace AshEngine
{
	namespace
	{
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

		static auto make_instance_data(const glm::mat4& object_to_clip) -> SceneStaticMeshInstanceData
		{
			SceneStaticMeshInstanceData data{};
			data.object_to_clip_col0 = object_to_clip[0];
			data.object_to_clip_col1 = object_to_clip[1];
			data.object_to_clip_col2 = object_to_clip[2];
			data.object_to_clip_col3 = object_to_clip[3];
			return data;
		}

		static void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
		{
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

			StaticMeshDrawBatch batch{};
			batch.program = program;
			batch.render_asset = render_asset;
			batch.vertex_buffer = render_asset && render_asset->resource ? render_asset->resource->vertex_buffer : nullptr;
			batch.index_buffer = render_asset && render_asset->resource ? render_asset->resource->index_buffer : nullptr;
			batch.first_index = section.first_index;
			batch.index_count = section.index_count;
			batches.push_back(std::move(batch));
			batch_lookup.emplace(key, batches.size() - 1);
			return batches.back();
		}

	}

	bool SceneRenderer::initialize(Renderer* renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_deferred_lighting_pass.initialize(m_renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SceneRenderer::shutdown()
	{
		m_deferred_lighting_pass.shutdown();
		m_instance_buffers.clear();
		m_logged_warning_keys.clear();
		m_logged_material_usage_keys.clear();
		m_renderer = nullptr;
	}

	bool SceneRenderer::should_use_instanced_static_mesh_path(size_t visible_static_mesh_draw_count)
	{
		return visible_static_mesh_draw_count > 1;
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

	bool SceneRenderer::render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context)
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

		const uint32_t output_width = view_context.output_target->get_width();
		const uint32_t output_height = view_context.output_target->get_height();
		const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();
		ASH_PROFILE_PLOT("Scene/GBufferTargets", static_cast<int64_t>(layout.attachments.size()));
		ASH_PROCESS_ERROR(output_width <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
		ASH_PROCESS_ERROR(output_height <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));

		RenderGraphBuilder graph(*m_renderer, view_context.debug_name ? view_context.debug_name : "SceneRenderGraph");
		RenderGraphTextureRef output = graph.register_external_texture(view_context.output_target, "SceneOutput");

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

		RenderGraphTextureDesc depth_desc{};
		depth_desc.width = static_cast<uint16_t>(output_width);
		depth_desc.height = static_cast<uint16_t>(output_height);
		depth_desc.format = RenderTextureFormat::D32_SFLOAT;
		depth_desc.shader_resource = true;
		depth_desc.unordered_access = false;
		depth_desc.use_optimized_clear_value = true;
		depth_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
		graph_resources.depth = graph.create_texture(depth_desc, "SceneDeferredDepth");

		RenderGraphTextureDesc lighting_desc{};
		lighting_desc.width = static_cast<uint16_t>(output_width);
		lighting_desc.height = static_cast<uint16_t>(output_height);
		lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
		lighting_desc.shader_resource = true;
		lighting_desc.unordered_access = false;
		lighting_desc.use_optimized_clear_value = true;
		lighting_desc.optimized_clear_color = {};
		graph_resources.lighting_accum = graph.create_texture(lighting_desc, "SceneDeferredLightingAccum");

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
			[this, &frame, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneGBufferPass", AshEngine::Profile::Color::Draw);
				return render_static_meshes_to_pass(frame, view_context, context, PassFamily::GBuffer);
			}));

		ASH_PROCESS_ERROR(m_deferred_lighting_pass.add_passes(
			graph,
			frame,
			graph_resources,
			output,
			view_context));
		ASH_PROCESS_ERROR(graph.execute());
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::render_static_meshes_to_pass(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& pass_context,
		PassFamily pass_family)
	{
		ASH_PROFILE_SCOPE_NC("SceneRenderer::render_static_meshes_to_pass", AshEngine::Profile::Color::Draw);
		const char* pass_label = get_staticmesh_pass_label(pass_family);
		ASH_PROFILE_SCOPE_TEXT(pass_label, std::strlen(pass_label));
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.static_mesh_draws.size()));
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const bool use_instanced_static_mesh_path =
			should_use_instanced_static_mesh_path(frame.static_mesh_draws.size());
		if (!use_instanced_static_mesh_path)
		{
			if (!frame.static_mesh_draws.empty())
			{
				const VisibleStaticMeshDraw& draw = frame.static_mesh_draws.front();
				ASH_PROCESS_ERROR(validate_static_mesh_draw_asset(draw));
				const SceneStaticMeshInstanceData instance_data =
					make_instance_data(frame.view_projection * draw.world_transform);
				std::shared_ptr<VertexBuffer> instance_buffer = ensure_instance_buffer(0, &instance_data, 1);
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
			std::vector<StaticMeshDrawBatch> batches{};
			batches.reserve(frame.static_mesh_draws.size());
			std::unordered_map<StaticMeshDrawBatchKey, size_t, StaticMeshDrawBatchKeyHash> batch_lookup{};
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

					StaticMeshDrawBatch& batch = find_or_add_batch(batches, batch_lookup, program, draw.render_asset, section);
					batch.instances.push_back(make_instance_data(frame.view_projection * draw.world_transform));
				}
			}

			ASH_PROFILE_PLOT("Scene/StaticMeshBatches", static_cast<int64_t>(batches.size()));
			for (size_t batch_index = 0; batch_index < batches.size(); ++batch_index)
			{
				StaticMeshDrawBatch& batch = batches[batch_index];
				ASH_PROCESS_ERROR(batch.program && batch.vertex_buffer && batch.index_buffer && !batch.instances.empty());
				const uint32_t instance_count = static_cast<uint32_t>(batch.instances.size());
				std::shared_ptr<VertexBuffer> instance_buffer =
					ensure_instance_buffer(batch_index, batch.instances.data(), instance_count);
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
