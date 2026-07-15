#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Function/Render/RenderGraphExecutor.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include <utility>

namespace AshEngine
{
	bool execute_render_graph(
		Renderer& renderer,
		std::vector<RenderGraphTextureNode>& textures,
		std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes);

	namespace
	{
		bool validate_buffer_usages(
			const std::string& graph_name,
			const RenderGraphPassNode& pass,
			const std::vector<RenderGraphBufferNode>& buffers)
		{
			for (const RenderGraphBufferUsage& usage : pass.buffer_usages)
			{
				if (!usage.buffer.is_valid() || usage.buffer.index >= buffers.size())
				{
					HLogError(
						"RenderGraph '{}': pass '{}' references invalid buffer {}.",
						graph_name,
						pass.name,
						usage.buffer.index);
					return false;
				}

				const RenderGraphBufferNode& buffer = buffers[usage.buffer.index];
				RenderGraphAccess expected_access = RenderGraphAccess::Unknown;
				if (usage.write)
				{
					expected_access = pass.kind == RenderGraphPassKind::Raster ?
						RenderGraphAccess::GraphicsUAV : RenderGraphAccess::ComputeUAV;
					if (usage.access != expected_access || !buffer.desc.unordered_access)
					{
						HLogError(
							"RenderGraph '{}': pass '{}' cannot write buffer '{}' with access '{}'.",
							graph_name,
							pass.name,
							buffer.name,
							render_graph_access_name(usage.access));
						return false;
					}
					continue;
				}

				if (usage.access == RenderGraphAccess::IndirectArgs)
				{
					if (!buffer.desc.indirect_args)
					{
						HLogError(
							"RenderGraph '{}': pass '{}' reads buffer '{}' as IndirectArgs without indirect usage.",
							graph_name,
							pass.name,
							buffer.name);
						return false;
					}
					continue;
				}

				expected_access = pass.kind == RenderGraphPassKind::Raster ?
					RenderGraphAccess::GraphicsSRV : RenderGraphAccess::ComputeSRV;
				if (usage.access != expected_access || !buffer.desc.shader_resource)
				{
					HLogError(
						"RenderGraph '{}': pass '{}' cannot read buffer '{}' with access '{}'.",
						graph_name,
						pass.name,
						buffer.name,
						render_graph_access_name(usage.access));
					return false;
				}
			}
			return true;
		}
	}

	RenderGraphBuilder::RenderGraphBuilder(Renderer& renderer, const char* name)
		: RenderGraphBuilder(&renderer, name)
	{
	}

	RenderGraphBuilder::RenderGraphBuilder(Renderer* renderer, const char* name)
		: m_renderer(renderer)
		, m_name(name ? name : "RenderGraph")
	{
	}

	RenderGraphBuilder RenderGraphBuilder::create_headless_for_tests(const char* name)
	{
		return RenderGraphBuilder(nullptr, name);
	}

	RenderGraphTextureRef RenderGraphBuilder::register_external_texture(
		const std::shared_ptr<RenderTarget>& texture,
		const char* name,
		RenderGraphAccess)
	{
		if (!texture)
		{
			HLogError("RenderGraph '{}': cannot register null external texture '{}'.", m_name, name ? name : "<unnamed>");
			return {};
		}

		RenderGraphTextureNode node{};
		node.name = name ? name : "ExternalTexture";
		node.external_texture = texture;
		node.external = true;
		node.desc.width = static_cast<uint16_t>(texture->get_width());
		node.desc.height = static_cast<uint16_t>(texture->get_height());
		node.desc.format = texture->get_format();
		node.desc.shader_resource = !texture->is_depth_stencil();
		node.desc.unordered_access = false;
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	RenderGraphTextureRef RenderGraphBuilder::register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name)
	{
		RenderGraphTextureNode node{};
		node.name = name ? name : "ExternalTextureForTests";
		node.external = true;
		node.desc = RenderGraphTextureDesc::from_render_target_desc(desc);
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	RenderGraphTextureRef RenderGraphBuilder::create_texture(const RenderGraphTextureDesc& desc, const char* name)
	{
		RenderGraphTextureNode node{};
		node.name = name ? name : "RenderGraphTexture";
		node.desc = desc;
		node.external = false;
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	void RenderGraphBuilder::extract_texture(RenderGraphTextureRef texture)
	{
		if (texture.index < m_textures.size())
		{
			m_textures[texture.index].extracted = true;
		}
	}

	RenderGraphBufferRef RenderGraphBuilder::register_external_buffer(
		const std::shared_ptr<StorageBuffer>& buffer,
		const char* name,
		RenderGraphAccess initial_access)
	{
		if (!buffer || buffer->get_size() == 0u)
		{
			HLogError("RenderGraph '{}': cannot register invalid external buffer '{}'.", m_name, name ? name : "<unnamed>");
			return {};
		}

		RenderGraphBufferNode node{};
		node.name = name ? name : "ExternalBuffer";
		node.desc.size = buffer->get_size();
		node.desc.stride = buffer->get_stride();
		node.desc.shader_resource = true;
		node.desc.unordered_access = true;
		node.desc.indirect_args = buffer->is_indirect_args();
		node.external_buffer = buffer;
		node.external = true;
		node.initial_access = initial_access;
		m_buffers.push_back(std::move(node));
		return { static_cast<uint32_t>(m_buffers.size() - 1u) };
	}

	RenderGraphBufferRef RenderGraphBuilder::register_external_buffer_desc_for_tests(
		const RenderGraphBufferDesc& desc,
		const char* name,
		RenderGraphAccess initial_access)
	{
		if (desc.size == 0u)
		{
			HLogError("RenderGraph '{}': cannot register zero-sized external buffer '{}'.", m_name, name ? name : "<unnamed>");
			return {};
		}

		RenderGraphBufferNode node{};
		node.name = name ? name : "ExternalBufferForTests";
		node.desc = desc;
		node.external = true;
		node.initial_access = initial_access;
		m_buffers.push_back(std::move(node));
		return { static_cast<uint32_t>(m_buffers.size() - 1u) };
	}

	RenderGraphBufferRef RenderGraphBuilder::create_buffer(const RenderGraphBufferDesc& desc, const char* name)
	{
		if (desc.size == 0u)
		{
			HLogError("RenderGraph '{}': cannot create zero-sized buffer '{}'.", m_name, name ? name : "<unnamed>");
			return {};
		}

		RenderGraphBufferNode node{};
		node.name = name ? name : "RenderGraphBuffer";
		node.desc = desc;
		m_buffers.push_back(std::move(node));
		return { static_cast<uint32_t>(m_buffers.size() - 1u) };
	}

	void RenderGraphBuilder::extract_buffer(RenderGraphBufferRef buffer)
	{
		if (buffer.index < m_buffers.size())
		{
			m_buffers[buffer.index].extracted = true;
		}
	}

	bool RenderGraphBuilder::add_raster_pass(
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
		const std::function<bool(RenderGraphRasterContext&)>& execute)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphBuilder::add_raster_pass", AshEngine::Profile::Color::Submit);
		if (timing_metric != RHI::GpuTimingMetric::Invalid &&
			!is_render_graph_gpu_timing_group_metric(timing_metric))
		{
			HLogError(
				"RenderGraph '{}': raster pass '{}' uses a timing metric reserved outside RenderGraph.",
				m_name,
				name ? name : "RasterPass");
			return false;
		}
		RenderGraphPassNode pass{};
		pass.name = name ? name : "RasterPass";
		ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
		pass.kind = RenderGraphPassKind::Raster;
		pass.flags = flags | RenderGraphPassFlags::Raster;
		pass.timing_metric = timing_metric;
		pass.raster_execute = execute;
		RenderGraphRasterPassBuilder builder(pass);
		if (setup)
		{
			setup(builder);
		}
		if (!validate_buffer_usages(m_name, pass, m_buffers))
		{
			return false;
		}
		m_passes.push_back(std::move(pass));
		return true;
	}

	bool RenderGraphBuilder::add_compute_pass(
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphComputePassBuilder&)>& setup,
		const std::function<bool(RenderGraphComputeContext&)>& execute)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphBuilder::add_compute_pass", AshEngine::Profile::Color::Submit);
		if (timing_metric != RHI::GpuTimingMetric::Invalid &&
			!is_render_graph_gpu_timing_group_metric(timing_metric))
		{
			HLogError(
				"RenderGraph '{}': compute pass '{}' uses a timing metric reserved outside RenderGraph.",
				m_name,
				name ? name : "ComputePass");
			return false;
		}
		RenderGraphPassNode pass{};
		pass.name = name ? name : "ComputePass";
		ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
		pass.kind = RenderGraphPassKind::Compute;
		pass.flags = flags | RenderGraphPassFlags::Compute;
		pass.timing_metric = timing_metric;
		pass.compute_execute = execute;
		RenderGraphComputePassBuilder builder(pass);
		if (setup)
		{
			setup(builder);
		}
		if (!validate_buffer_usages(m_name, pass, m_buffers))
		{
			return false;
		}
		m_passes.push_back(std::move(pass));
		return true;
	}

	bool add_render_graph_raster_pass_for_tests(
		RenderGraphBuilder& graph,
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
		const std::function<bool(RenderGraphRasterContext&)>& execute)
	{
		return graph.add_raster_pass(name, flags, timing_metric, setup, execute);
	}

	bool add_render_graph_compute_pass_for_tests(
		RenderGraphBuilder& graph,
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphComputePassBuilder&)>& setup,
		const std::function<bool(RenderGraphComputeContext&)>& execute)
	{
		return graph.add_compute_pass(name, flags, timing_metric, setup, execute);
	}

	bool RenderGraphBuilder::execute()
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphBuilder::execute", AshEngine::Profile::Color::Render);
		ASH_PROFILE_SCOPE_TEXT(m_name.c_str(), m_name.size());
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(m_passes.size()));
		if (!m_renderer)
		{
			HLogError("RenderGraph '{}': execute requires a renderer.", m_name);
			return false;
		}
		return execute_render_graph(*m_renderer, m_textures, m_buffers, m_passes);
	}

	bool RenderGraphBuilder::compile_for_tests(RenderGraphCompileResult& out_result) const
	{
		return RenderGraphCompiler::compile(m_textures, m_buffers, m_passes, out_result);
	}

	bool RenderGraphBuilder::compile_cached_for_tests(RenderGraphCompileResult& out_result) const
	{
		return RenderGraphCompiler::compile_cached(m_textures, m_buffers, m_passes, out_result);
	}

	size_t RenderGraphBuilder::get_texture_count_for_tests() const
	{
		return m_textures.size();
	}

	size_t RenderGraphBuilder::get_pass_count_for_tests() const
	{
		return m_passes.size();
	}

	const std::vector<RenderGraphTextureNode>& RenderGraphBuilder::get_textures_for_tests() const
	{
		return m_textures;
	}

	size_t RenderGraphBuilder::get_buffer_count_for_tests() const
	{
		return m_buffers.size();
	}

	const std::vector<RenderGraphBufferNode>& RenderGraphBuilder::get_buffers_for_tests() const
	{
		return m_buffers;
	}

	const std::vector<RenderGraphPassNode>& RenderGraphBuilder::get_passes_for_tests() const
	{
		return m_passes;
	}
}
