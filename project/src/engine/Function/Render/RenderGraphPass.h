#pragma once

#include "Function/Render/Renderer.h"
#include "Function/Render/RenderGraphResource.h"
#include "Graphics/GpuTimingTelemetryRHI.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class RenderGraphPassFlags : uint8_t
	{
		None = 0,
		Raster = 1 << 0,
		Compute = 1 << 1,
		NeverCull = 1 << 2
	};

	inline RenderGraphPassFlags operator|(RenderGraphPassFlags lhs, RenderGraphPassFlags rhs)
	{
		return static_cast<RenderGraphPassFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
	}

	inline bool has_render_graph_pass_flag(RenderGraphPassFlags flags, RenderGraphPassFlags flag)
	{
		return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
	}

	enum class RenderGraphPassKind : uint8_t
	{
		Raster = 0,
		Compute
	};

	struct RenderGraphTextureUsage
	{
		RenderGraphTextureRef texture{};
		RenderGraphAccess access = RenderGraphAccess::Unknown;
		uint8_t color_slot = UINT8_MAX;
		bool depth = false;
		RenderLoadAction load_action = RenderLoadAction::Load;
		RenderColorValue clear_color{};
		RenderDepthStencilValue clear_depth{};
		RenderGraphDepthReadMode depth_read_mode = RenderGraphDepthReadMode::DepthTestOnly;
	};

	struct RenderGraphBufferUsage
	{
		RenderGraphBufferRef buffer{};
		RenderGraphAccess access = RenderGraphAccess::Unknown;
		bool write = false;
	};

	class RenderGraphRasterContext;
	class RenderGraphComputeContext;

	struct RenderGraphPassNode
	{
		std::string name{};
		RenderGraphPassKind kind = RenderGraphPassKind::Raster;
		RenderGraphPassFlags flags = RenderGraphPassFlags::None;
		RHI::GpuTimingMetric timing_metric = RHI::GpuTimingMetric::Invalid;
		std::vector<RenderGraphTextureUsage> texture_usages{};
		std::vector<RenderGraphBufferUsage> buffer_usages{};
		std::function<bool(RenderGraphRasterContext&)> raster_execute{};
		std::function<bool(RenderGraphComputeContext&)> compute_execute{};
	};

	inline bool is_render_graph_gpu_timing_group_metric(RHI::GpuTimingMetric metric)
	{
		return metric > RHI::GpuTimingMetric::Frame && metric < RHI::GpuTimingMetric::Count;
	}

	class RenderGraphRasterPassBuilder
	{
	public:
		explicit RenderGraphRasterPassBuilder(RenderGraphPassNode& pass);
		void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
		ASH_API void read_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access);
		ASH_API void write_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access);
		void write_color(uint8_t slot, RenderGraphTextureRef texture, RenderLoadAction load_action, RenderColorValue clear_color);
		void write_depth(RenderGraphTextureRef texture, RenderLoadAction load_action, RenderDepthStencilValue clear_value);
		void read_depth(RenderGraphTextureRef texture, RenderGraphDepthReadMode mode);

	private:
		RenderGraphPassNode* m_pass = nullptr;
	};

	class RenderGraphComputePassBuilder
	{
	public:
		explicit RenderGraphComputePassBuilder(RenderGraphPassNode& pass);
		void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
		void write_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
		ASH_API void read_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access);
		ASH_API void write_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access);

	private:
		RenderGraphPassNode* m_pass = nullptr;
	};

	class RenderGraphRasterContext
	{
	public:
		virtual ~RenderGraphRasterContext() = default;
		virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
		virtual std::shared_ptr<StorageBuffer> get_buffer(RenderGraphBufferRef buffer) = 0;
		virtual bool draw(const GraphicsDrawDesc& desc) = 0;
	};

	class RenderGraphComputeContext
	{
	public:
		virtual ~RenderGraphComputeContext() = default;
		virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
		virtual std::shared_ptr<StorageBuffer> get_buffer(RenderGraphBufferRef buffer) = 0;
		virtual bool dispatch(const ComputeDispatchDesc& desc) = 0;
	};
}
