#include "Function/Render/RenderGraphExecutor.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include <algorithm>

namespace AshEngine
{
	namespace
	{
		// Internal non-owning guard. RenderGraph owns only pass-group transitions;
		// GPU.Frame remains owned by RenderDevice.
		class RenderGraphGpuTimingScopeGuard
		{
		public:
			RenderGraphGpuTimingScopeGuard(
				RHI::IGpuTimingTelemetry* telemetry,
				RHI::CommandBuffer* command_buffer)
				: m_telemetry(telemetry)
				, m_command_buffer(command_buffer)
			{
			}

			~RenderGraphGpuTimingScopeGuard()
			{
				close();
			}

			RenderGraphGpuTimingScopeGuard(const RenderGraphGpuTimingScopeGuard&) = delete;
			RenderGraphGpuTimingScopeGuard& operator=(const RenderGraphGpuTimingScopeGuard&) = delete;

			void transition_to(RHI::GpuTimingMetric metric)
			{
				const RHI::GpuTimingMetric next_group =
					is_render_graph_gpu_timing_group_metric(metric) ? metric : RHI::GpuTimingMetric::Invalid;
				if (next_group == m_current_group)
				{
					return;
				}

				if (m_scope_open && m_telemetry && m_command_buffer)
				{
					m_telemetry->end_scope(m_command_buffer, m_current_group);
				}
				m_scope_open = false;
				m_current_group = next_group;

				if (m_current_group != RHI::GpuTimingMetric::Invalid && m_telemetry && m_command_buffer)
				{
					m_scope_open = m_telemetry->begin_scope(m_command_buffer, m_current_group);
				}
			}

			void close()
			{
				if (m_scope_open && m_telemetry && m_command_buffer)
				{
					m_telemetry->end_scope(m_command_buffer, m_current_group);
				}
				m_scope_open = false;
				m_current_group = RHI::GpuTimingMetric::Invalid;
			}

		private:
			RHI::IGpuTimingTelemetry* m_telemetry = nullptr;
			RHI::CommandBuffer* m_command_buffer = nullptr;
			RHI::GpuTimingMetric m_current_group = RHI::GpuTimingMetric::Invalid;
			bool m_scope_open = false;
		};

		class RasterContext final : public RenderGraphRasterContext
		{
		public:
			RasterContext(
				Renderer::GraphicsPassContext& pass_context,
				std::vector<RenderGraphTextureNode>& textures,
				std::vector<RenderGraphBufferNode>& buffers)
				: m_pass_context(pass_context)
				, m_textures(textures)
				, m_buffers(buffers)
			{
			}

			std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
			{
				return texture.index < m_textures.size() ? m_textures[texture.index].external_texture : nullptr;
			}

			std::shared_ptr<StorageBuffer> get_buffer(RenderGraphBufferRef buffer) override
			{
				return buffer.index < m_buffers.size() ? m_buffers[buffer.index].external_buffer : nullptr;
			}

			bool draw(const GraphicsDrawDesc& desc) override
			{
				return m_pass_context.draw(desc);
			}

		private:
			Renderer::GraphicsPassContext& m_pass_context;
			std::vector<RenderGraphTextureNode>& m_textures;
			std::vector<RenderGraphBufferNode>& m_buffers;
		};

		class ComputeContext final : public RenderGraphComputeContext
		{
		public:
			ComputeContext(
				Renderer* renderer,
				std::vector<RenderGraphTextureNode>& textures,
				std::vector<RenderGraphBufferNode>& buffers,
				const RenderGraphBufferBindingScope* graph_buffer_binding_scope)
				: m_renderer(renderer)
				, m_textures(textures)
				, m_buffers(buffers)
				, m_graph_buffer_binding_scope(graph_buffer_binding_scope)
			{
			}

			std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
			{
				return texture.index < m_textures.size() ? m_textures[texture.index].external_texture : nullptr;
			}

			std::shared_ptr<StorageBuffer> get_buffer(RenderGraphBufferRef buffer) override
			{
				return buffer.index < m_buffers.size() ? m_buffers[buffer.index].external_buffer : nullptr;
			}

			bool dispatch(const ComputeDispatchDesc& desc) override
			{
				ComputeDispatchDesc graph_desc = desc;
				graph_desc.graph_buffer_binding_scope = m_graph_buffer_binding_scope;
				return m_renderer && m_renderer->dispatch(graph_desc);
			}

		private:
			Renderer* m_renderer = nullptr;
			std::vector<RenderGraphTextureNode>& m_textures;
			std::vector<RenderGraphBufferNode>& m_buffers;
			const RenderGraphBufferBindingScope* m_graph_buffer_binding_scope = nullptr;
		};

		bool texture_is_depth_format(RenderTextureFormat format)
		{
			return format == RenderTextureFormat::D24_UNORM_S8_UINT ||
				format == RenderTextureFormat::D32_SFLOAT;
		}

		bool execution_ops_are_valid(const RenderGraphExecutionOps& ops)
		{
			return ops.acquire_transient_storage_buffer &&
				ops.release_transient_storage_buffer &&
				ops.submit_buffer_transitions &&
				ops.begin_raster_pass &&
				ops.end_raster_pass;
		}

		std::shared_ptr<StorageBuffer> acquire_production_buffer(void* user_data, const StorageBufferDesc& desc)
		{
			Renderer* renderer = static_cast<Renderer*>(user_data);
			return renderer ? renderer->acquire_transient_storage_buffer(desc) : nullptr;
		}

		void release_production_buffer(void* user_data, const std::shared_ptr<StorageBuffer>& buffer)
		{
			Renderer* renderer = static_cast<Renderer*>(user_data);
			if (renderer)
			{
				renderer->release_transient_storage_buffer(buffer);
			}
		}

		bool submit_production_transitions(
			void* user_data,
			const RenderGraphResolvedBufferTransition* transitions,
			size_t transition_count)
		{
			Renderer* renderer = static_cast<Renderer*>(user_data);
			return renderer && submit_render_graph_buffer_transitions(*renderer, transitions, transition_count);
		}

		bool begin_production_raster(
			void* user_data,
			const PassDesc& desc,
			Renderer::GraphicsPassContext& pass_context)
		{
			Renderer* renderer = static_cast<Renderer*>(user_data);
			return renderer && renderer->begin_pass(desc, pass_context);
		}

		bool end_production_raster(void*, Renderer::GraphicsPassContext& pass_context)
		{
			return pass_context.end();
		}

		RenderGraphExecutionOps make_production_ops(Renderer& renderer)
		{
			RenderGraphExecutionOps ops{};
			ops.user_data = &renderer;
			ops.acquire_transient_storage_buffer = &acquire_production_buffer;
			ops.release_transient_storage_buffer = &release_production_buffer;
			ops.submit_buffer_transitions = &submit_production_transitions;
			ops.begin_raster_pass = &begin_production_raster;
			ops.end_raster_pass = &end_production_raster;
			return ops;
		}
	}

	static bool execute_render_graph_core(
		Renderer* renderer,
		std::vector<RenderGraphTextureNode>& textures,
		std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		const RenderGraphExecutionOps& ops,
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraph::execute", AshEngine::Profile::Color::Render);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(passes.size()));
		if (!execution_ops_are_valid(ops))
		{
			HLogError("RenderGraph: execution operations are incomplete.");
			return false;
		}

		RenderGraphCompileResult compiled{};
		if (!RenderGraphCompiler::compile_cached(textures, buffers, passes, compiled))
		{
			return false;
		}
		ASH_PROFILE_PLOT("RenderGraph/ExecutedPasses", static_cast<int64_t>(compiled.live_pass_indices.size()));

		std::vector<uint32_t> allocated_texture_indices{};
		std::vector<uint32_t> allocated_buffer_indices{};
		const auto release_allocated_transients = [&]()
		{
			ASH_PROFILE_SCOPE_NC("RenderGraph::ReleaseTransients", AshEngine::Profile::Color::Upload);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(allocated_texture_indices.size() + allocated_buffer_indices.size()));
			for (uint32_t buffer_index : allocated_buffer_indices)
			{
				std::shared_ptr<StorageBuffer>& buffer = buffers[buffer_index].external_buffer;
				ops.release_transient_storage_buffer(ops.user_data, buffer);
				buffer.reset();
			}
			allocated_buffer_indices.clear();
			if (renderer)
			{
				for (uint32_t texture_index : allocated_texture_indices)
				{
					std::shared_ptr<RenderTarget>& texture = textures[texture_index].external_texture;
					renderer->release_transient_render_target(texture);
					texture.reset();
				}
			}
			allocated_texture_indices.clear();
		};

		{
			ASH_PROFILE_SCOPE_NC("RenderGraph::AllocateTransients", AshEngine::Profile::Color::Upload);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(textures.size() + buffers.size()));
			for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
			{
				RenderGraphTextureNode& texture = textures[texture_index];
				const bool should_allocate = !texture.external &&
					texture_index < compiled.texture_lifetimes.size() &&
					compiled.texture_lifetimes[texture_index].used;
				if (!should_allocate)
				{
					continue;
				}

				if (!renderer)
				{
					release_allocated_transients();
					return false;
				}
				texture.external_texture = renderer->acquire_transient_render_target(
					texture.desc.to_render_target_desc(texture.name.c_str()));
				if (!texture.external_texture)
				{
					HLogError("RenderGraph: failed to allocate transient texture '{}'.", texture.name);
					release_allocated_transients();
					return false;
				}
				allocated_texture_indices.push_back(texture_index);
			}

			for (uint32_t buffer_index = 0; buffer_index < buffers.size(); ++buffer_index)
			{
				RenderGraphBufferNode& buffer = buffers[buffer_index];
				const bool should_allocate = !buffer.external &&
					buffer_index < compiled.buffer_lifetimes.size() &&
					compiled.buffer_lifetimes[buffer_index].used;
				if (!should_allocate)
				{
					continue;
				}

				buffer.external_buffer = ops.acquire_transient_storage_buffer(
					ops.user_data,
					buffer.desc.to_storage_buffer_desc(buffer.name.c_str()));
				if (!buffer.external_buffer)
				{
					HLogError("RenderGraph: failed to allocate transient buffer '{}'.", buffer.name);
					release_allocated_transients();
					return false;
				}
				allocated_buffer_indices.push_back(buffer_index);
			}
		}
		ASH_PROFILE_PLOT("RenderGraph/TransientTextures", static_cast<int64_t>(allocated_texture_indices.size()));
		ASH_PROFILE_PLOT("RenderGraph/TransientBuffers", static_cast<int64_t>(allocated_buffer_indices.size()));

		RenderGraphGpuTimingScopeGuard gpu_timing_scope(telemetry, command_buffer);
		const auto fail_execution = [&]()
		{
			gpu_timing_scope.close();
			release_allocated_transients();
			return false;
		};

		size_t maximum_buffer_transition_count = 0u;
		for (uint32_t pass_index : compiled.live_pass_indices)
		{
			maximum_buffer_transition_count = std::max(
				maximum_buffer_transition_count,
				compiled.pass_barriers[pass_index].buffer_transitions.size());
		}
		std::vector<RenderGraphResolvedBufferTransition> resolved_buffer_transitions{};
		resolved_buffer_transitions.reserve(maximum_buffer_transition_count);
		std::vector<RenderGraphResolvedBufferBinding> graph_owned_buffer_bindings{};
		graph_owned_buffer_bindings.reserve(buffers.size());
		for (const RenderGraphBufferNode& buffer : buffers)
		{
			if (buffer.external_buffer)
			{
				graph_owned_buffer_bindings.push_back({
					buffer.external_buffer.get(),
					buffer.name.c_str(),
					RenderGraphAccess::Unknown });
			}
		}
		std::vector<RenderGraphResolvedBufferBinding> declared_buffer_bindings{};

		for (uint32_t pass_index : compiled.live_pass_indices)
		{
			const RenderGraphPassNode& pass = passes[pass_index];
			declared_buffer_bindings.clear();
			declared_buffer_bindings.reserve(pass.buffer_usages.size());
			for (const RenderGraphBufferUsage& usage : pass.buffer_usages)
			{
				if (!usage.buffer || usage.buffer.index >= buffers.size() ||
					!buffers[usage.buffer.index].external_buffer)
				{
					HLogError("RenderGraph: pass '{}' has an unresolved declared buffer binding.", pass.name);
					return fail_execution();
				}
				const RenderGraphBufferNode& buffer = buffers[usage.buffer.index];
				const StorageBuffer* identity = buffer.external_buffer.get();
				const auto existing = std::find_if(
					declared_buffer_bindings.begin(),
					declared_buffer_bindings.end(),
					[identity](const RenderGraphResolvedBufferBinding& binding)
					{
						return binding.buffer == identity;
					});
				if (existing != declared_buffer_bindings.end())
				{
					if (existing->access != usage.access)
					{
						HLogError(
							"RenderGraph: pass '{}' declares conflicting access for buffer '{}'.",
							pass.name,
							buffer.name);
						return fail_execution();
					}
					continue;
				}
				declared_buffer_bindings.push_back({ identity, buffer.name.c_str(), usage.access });
			}
			const RenderGraphBufferBindingScope graph_binding_scope{
				pass.name.c_str(),
				graph_owned_buffer_bindings.data(),
				graph_owned_buffer_bindings.size(),
				declared_buffer_bindings.data(),
				declared_buffer_bindings.size() };
			gpu_timing_scope.transition_to(pass.timing_metric);
			resolved_buffer_transitions.clear();
			for (const RenderGraphBufferTransition& transition : compiled.pass_barriers[pass_index].buffer_transitions)
			{
				if (!transition.buffer || transition.buffer.index >= buffers.size())
				{
					HLogError("RenderGraph: pass '{}' has an invalid compiled buffer transition.", pass.name);
					return fail_execution();
				}
				const std::shared_ptr<StorageBuffer>& buffer = buffers[transition.buffer.index].external_buffer;
				if (!buffer)
				{
					HLogError("RenderGraph: pass '{}' references an unresolved buffer.", pass.name);
					return fail_execution();
				}
				resolved_buffer_transitions.push_back({ buffer, transition.state });
			}
			if (!resolved_buffer_transitions.empty() &&
				!ops.submit_buffer_transitions(
					ops.user_data,
					resolved_buffer_transitions.data(),
					resolved_buffer_transitions.size()))
			{
				HLogError("RenderGraph: buffer transitions failed for pass '{}'.", pass.name);
				return fail_execution();
			}

			if (pass.kind == RenderGraphPassKind::Compute)
			{
				ASH_PROFILE_SCOPE_NC("RenderGraph::ExecuteComputePass", AshEngine::Profile::Color::Submit);
				ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
				ComputeContext context(renderer, textures, buffers, &graph_binding_scope);
				if (!pass.compute_execute || !pass.compute_execute(context))
				{
					HLogError("RenderGraph: compute pass '{}' failed.", pass.name);
					return fail_execution();
				}
				continue;
			}

			ASH_PROFILE_SCOPE_NC("RenderGraph::ExecuteRasterPass", AshEngine::Profile::Color::Submit);
			ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
			PassDesc pass_desc{};
			pass_desc.name = pass.name.c_str();
			pass_desc.graph_buffer_binding_scope = &graph_binding_scope;
			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				if (!usage.texture || usage.texture.index >= textures.size())
				{
					HLogError("RenderGraph: raster pass '{}' has invalid texture usage.", pass.name);
					return fail_execution();
				}

				std::shared_ptr<RenderTarget> target = textures[usage.texture.index].external_texture;
				if (!target)
				{
					HLogError(
						"RenderGraph: raster pass '{}' references unallocated texture '{}'.",
						pass.name,
						textures[usage.texture.index].name);
					return fail_execution();
				}

				if (usage.access == RenderGraphAccess::ColorAttachmentWrite)
				{
					if (texture_is_depth_format(target->get_format()))
					{
						HLogError("RenderGraph: pass '{}' uses depth format as color attachment.", pass.name);
						return fail_execution();
					}
					if (pass_desc.color_attachments.size() <= usage.color_slot)
					{
						pass_desc.color_attachments.resize(static_cast<size_t>(usage.color_slot) + 1u);
					}
					PassColorAttachment& attachment = pass_desc.color_attachments[usage.color_slot];
					attachment.render_target = target;
					attachment.load_action = usage.load_action;
					attachment.clear_color = usage.clear_color;
				}
				else if (usage.access == RenderGraphAccess::DepthStencilWrite || usage.access == RenderGraphAccess::DepthStencilRead)
				{
					if (!texture_is_depth_format(target->get_format()))
					{
						HLogError("RenderGraph: pass '{}' uses color format as depth attachment.", pass.name);
						return fail_execution();
					}
					pass_desc.depth_attachment.render_target = target;
					pass_desc.depth_attachment.load_action = usage.load_action;
					pass_desc.depth_attachment.clear_value = usage.clear_depth;
					pass_desc.depth_attachment.read_only = usage.access == RenderGraphAccess::DepthStencilRead;
					pass_desc.depth_attachment.final_state = usage.access == RenderGraphAccess::DepthStencilRead ?
						render_graph_depth_read_state(usage.depth_read_mode) : RHI::AshResourceState::DSVWrite;
				}
			}

			Renderer::GraphicsPassContext pass_context{};
			if (!ops.begin_raster_pass(ops.user_data, pass_desc, pass_context))
			{
				HLogError("RenderGraph: begin raster pass '{}' failed.", pass.name);
				return fail_execution();
			}

			RasterContext context(pass_context, textures, buffers);
			const bool pass_result = pass.raster_execute && pass.raster_execute(context);
			const bool end_result = ops.end_raster_pass(ops.user_data, pass_context);
			if (!pass_result || !end_result)
			{
				HLogError("RenderGraph: raster pass '{}' failed.", pass.name);
				return fail_execution();
			}
		}

		gpu_timing_scope.close();
		release_allocated_transients();
		return true;
	}

	bool execute_render_graph(
		Renderer& renderer,
		std::vector<RenderGraphTextureNode>& textures,
		std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes)
	{
		RenderDevice* render_device = renderer.get_render_device();
		const RenderGraphExecutionOps ops = make_production_ops(renderer);
		return execute_render_graph_core(
			&renderer,
			textures,
			buffers,
			passes,
			ops,
			render_device ? render_device->get_gpu_timing_telemetry() : nullptr,
			render_device ? render_device->get_current_command_buffer() : nullptr);
	}

	bool execute_render_graph_with_ops_for_tests(
		std::vector<RenderGraphTextureNode>& textures,
		std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		const RenderGraphExecutionOps& ops)
	{
		return execute_render_graph_core(nullptr, textures, buffers, passes, ops, nullptr, nullptr);
	}

	bool execute_render_graph_for_tests(
		Renderer& renderer,
		std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer)
	{
		std::vector<RenderGraphBufferNode> buffers{};
		const RenderGraphExecutionOps ops = make_production_ops(renderer);
		return execute_render_graph_core(&renderer, textures, buffers, passes, ops, telemetry, command_buffer);
	}

	void run_render_graph_gpu_timing_scope_sequence_for_tests(
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer,
		const std::vector<RHI::GpuTimingMetric>& metrics)
	{
		RenderGraphGpuTimingScopeGuard scope(telemetry, command_buffer);
		for (RHI::GpuTimingMetric metric : metrics)
		{
			scope.transition_to(metric);
		}
	}
}
