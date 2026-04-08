#pragma once
#include "Graphics/Pipeline.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Graphics/Sampler.h"
#include "Base/hassert.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace RHI
{
	class Shader;
	class CommandBuffer;
	class AccelerationStructureView;

	class RenderState
	{
	public:
		RasterizationCreation rasterization{};
		DepthStencilCreation depth_stencil{};
		BlendStateCreation blend_state{};
		AshPrimitiveTopology primitive_topology = AshPrimitiveTopology::ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		RenderState()
		{
			sync_viewport_state();
		}

		void set_viewport(const Viewport& viewport)
		{
			viewport_count = 1;
			viewports[0] = viewport;
			sync_viewport_state();
		}

		void set_scissor(const Rect2DInt& scissor)
		{
			scissor_count = 1;
			scissors[0] = scissor;
			sync_viewport_state();
		}

		void set_viewport_state(const ViewportState& state)
		{
			viewport_count = std::min<uint32_t>(state.num_viewports, static_cast<uint32_t>(viewports.size()));
			scissor_count = std::min<uint32_t>(state.num_scissors, static_cast<uint32_t>(scissors.size()));
			for (uint32_t i = 0; i < viewport_count; ++i)
			{
				viewports[i] = state.viewport[i];
			}
			for (uint32_t i = 0; i < scissor_count; ++i)
			{
				scissors[i] = state.scissors[i];
			}
			sync_viewport_state();
		}

		void clear_viewport_state()
		{
			viewport_count = 0;
			scissor_count = 0;
			sync_viewport_state();
		}

		const ViewportState* get_viewport_state() const
		{
			return &viewport_state;
		}

	private:
		void sync_viewport_state()
		{
			viewport_state.num_viewports = viewport_count;
			viewport_state.num_scissors = scissor_count;
			viewport_state.viewport = viewport_count > 0 ? viewports.data() : nullptr;
			viewport_state.scissors = scissor_count > 0 ? scissors.data() : nullptr;
		}

	private:
		ViewportState viewport_state{};
		std::array<Viewport, 8> viewports{};
		std::array<Rect2DInt, 8> scissors{};
		uint32_t viewport_count = 0;
		uint32_t scissor_count = 0;
	};

	class IRenderProgramBinder
	{
	public:
		IRenderProgramBinder() = default;
		virtual ~IRenderProgramBinder() = default;

		virtual IRenderProgramBinder& begin_bind() = 0;
		virtual IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<BufferView> uav) = 0;
		virtual IRenderProgramBinder& add_bind_uav(const char* name, std::shared_ptr<TextureView> uav) = 0;
		virtual IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& uavs) = 0;
		virtual IRenderProgramBinder& add_bind_uav_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& uavs) = 0;
		virtual IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<BufferView> srv) = 0;
		virtual IRenderProgramBinder& add_bind_srv(const char* name, std::shared_ptr<TextureView> srv) = 0;
		virtual IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<BufferView>>& srvs) = 0;
		virtual IRenderProgramBinder& add_bind_srv_array(const char* name, const std::vector<std::shared_ptr<TextureView>>& srvs) = 0;
		virtual IRenderProgramBinder& add_bind_cbv(const char* name, std::shared_ptr<BufferView> cbv) = 0;
		virtual IRenderProgramBinder& add_bind_sampler(const char* name, std::shared_ptr<Sampler> sampler) = 0;
		virtual IRenderProgramBinder& add_bind_sampler_array(const char* name, const std::vector<std::shared_ptr<Sampler>>& samplers) = 0;
		virtual IRenderProgramBinder& add_bind_acceleration_structure(const char* name, std::shared_ptr<AccelerationStructureView> acceleration_structure) = 0;
		virtual IRenderProgramBinder& set_const_data_block(uint32_t size, const void* data) = 0;
		virtual IRenderProgramBinder& set_immutable_const_value_int(const char* name, int32_t value) = 0;
		virtual IRenderProgramBinder& set_immutable_const_value_uint(const char* name, uint32_t value) = 0;
		virtual IRenderProgramBinder& set_immutable_const_value_float(const char* name, float value) = 0;
		virtual bool is_binding() const = 0;
	};

	struct GraphicProgramCreateDesc
	{
		PipelineCreation pipeline{};
	};

	struct ComputeProgramCreateDesc
	{
		PipelineCreation pipeline{};
	};

	class IGraphicsRenderProgram
	{
	public:
		IGraphicsRenderProgram() = default;
		virtual ~IGraphicsRenderProgram() = default;

		virtual bool create(const GraphicProgramCreateDesc& desc) = 0;
		virtual bool destroy() = 0;
		virtual bool apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall) = 0;
		virtual bool set_const_data_block(uint32_t size, const void* data) = 0;
		virtual bool apply(std::shared_ptr<CommandBuffer> cb) = 0;
		virtual IRenderProgramBinder& begin_bind() = 0;
		virtual bool end_bind() = 0;
	};

	class IComputeRenderProgram
	{
	public:
		IComputeRenderProgram() = default;
		virtual ~IComputeRenderProgram() = default;

		virtual bool create(const ComputeProgramCreateDesc& desc) = 0;
		virtual bool destroy() = 0;
		virtual bool set_const_data_block(uint32_t size, const void* data) = 0;
		virtual bool apply(std::shared_ptr<CommandBuffer> cb) = 0;
		virtual IRenderProgramBinder& begin_bind() = 0;
		virtual bool end_bind() = 0;
	};

	class IRayTracingRenderProgram
	{
	public:
		IRayTracingRenderProgram() = default;
		virtual ~IRayTracingRenderProgram() = default;
	};
}
