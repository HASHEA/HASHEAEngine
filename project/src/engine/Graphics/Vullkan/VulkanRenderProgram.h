#pragma once
#include "Graphics/RenderProgram.h"
#include "VulkanWrapper.h"
#include <memory>
namespace RHI
{
	class VulkanDescriptorSet;
	class VulkanPipeline;
	class VulkanGraphicsRenderProgram : public IGraphicsRenderProgram
	{
	public:
		VulkanGraphicsRenderProgram() = default;
		~VulkanGraphicsRenderProgram() = default;
	public:
		bool create(std::shared_ptr<Shader>, const GraphicProgramCreateDesc& desc);
		bool destroy();
		bool apply(std::shared_ptr<CommandBuffer> cb) override;
		IRenderProgramBinder& begin_bind() override;
		bool end_bind() override;
	private:
		bool refresh_pipeline();
	private:
		std::unique_ptr<VulkanPipeline> m_pPipeline = nullptr;
		std::vector<std::shared_ptr<VulkanDescriptorSet>> m_vecSets;

		// Í¨¹ý IGraphicsRenderProgram ¼Ì³Ð
		bool apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall) override;
	};
}