#pragma once
#include "Graphics/RenderPass.h"
#include <memory>
namespace RHI
{
	class VulkanRenderPass : public RenderPass
	{
	public:
		VulkanRenderPass(const RenderPassCreation& ci);
		~VulkanRenderPass();
	public:
		static auto create(const RenderPassCreation& ci) -> std::shared_ptr<VulkanRenderPass>;
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
		auto get_color_operations() -> const Array<AshLoadOption> & override;

		auto get_depth_stencil_operations() -> AshLoadOption override;

		auto get_depth_stencil_format() -> AshFormat override;
	private:
		VkRenderPass vkRenderPass = VK_NULL_HANDLE;
		const char* name = nullptr;
		Array<AshLoadOption> colorLoadOptions;
		AshLoadOption depthStencilLoadOption = AshLoadOption::ASH_LOAD_DONT_CARE;
		AshFormat depthStencilFormat = ASH_FORMAT_UNDEFINED;

		

	};

	
}