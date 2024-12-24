#pragma once
#include "Graphics/Framebuffer.h"
#include "VulkanHelper.hpp"
namespace RHI
{
	class VulkanFramebuffer : public Framebuffer	
	{
	public:
		VulkanFramebuffer(const FramebufferCreation& ci);
		~VulkanFramebuffer();
		static auto create(const FramebufferCreation& ci) -> std::shared_ptr<VulkanFramebuffer>;
	public:
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	private:
		VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
		const char* name = nullptr;
		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t layers = 0;
		//hold ref of attachments:
		Array<std::shared_ptr<TextureView>> attachmentsRef;

		

	};
}