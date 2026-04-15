#include "ImGuiLayer.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Function/Render/RenderDevice.h"
#include "Graphics/RenderPass.h"
#include "Graphics/Swapchain.h"
#include "Graphics/Texture.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "GLFW/glfw3.h"

#if defined(ASH_HAS_VULKAN)
#include "Graphics/Vullkan/VulkanCommandBuffer.h"
#include "Graphics/Vullkan/VulkanContext.h"
#include "Graphics/Vullkan/VulkanHelper.hpp"
#include "Graphics/Vullkan/VulkanRenderPass.h"
#include "Graphics/Vullkan/VulkanSampler.h"
#include "Graphics/Vullkan/VulkanTexture.h"
#include "imgui_impl_vulkan.h"
#endif

#if defined(ASH_HAS_DX12)
#include "Graphics/DirectX12/DX12CommandBuffer.h"
#include "Graphics/DirectX12/DX12Context.h"
#include "Graphics/DirectX12/DX12Helper.hpp"
#include "Graphics/DirectX12/DX12TextureView.h"
#include "imgui_impl_dx12.h"
#endif

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static auto to_rhi_format(RenderTextureFormat format) -> RHI::AshFormat
		{
			switch (format)
			{
			case RenderTextureFormat::RGBA8_UNORM:
				return RHI::ASH_FORMAT_R8G8B8A8_UNORM;
			case RenderTextureFormat::BGRA8_SRGB:
				return RHI::ASH_FORMAT_B8G8R8A8_SRGB;
			case RenderTextureFormat::RGBA16_SFLOAT:
				return RHI::ASH_FORMAT_R16G16B16A16_SFLOAT;
			case RenderTextureFormat::RGBA32_SFLOAT:
				return RHI::ASH_FORMAT_R32G32B32A32_SFLOAT;
			case RenderTextureFormat::D24_UNORM_S8_UINT:
				return RHI::ASH_FORMAT_D24_UNORM_S8_UINT;
			case RenderTextureFormat::D32_SFLOAT:
				return RHI::ASH_FORMAT_D32_SFLOAT;
			default:
				return RHI::ASH_FORMAT_UNDEFINED;
			}
		}

#if defined(ASH_HAS_VULKAN)
		static void check_imgui_vk_result(VkResult result)
		{
			if (result != VK_SUCCESS)
			{
				HLogError("ImGui Vulkan backend returned VkResult {}.", static_cast<int32_t>(result));
			}
		}
#endif
	}

	class NativeImGuiLayer final : public ImGuiLayer
	{
	public:
		~NativeImGuiLayer() override
		{
			shutdown();
		}

	public:
		bool init(Window* window, RHI::GraphicsContext* graphics_context, RenderDevice* render_device, const UIContextConfig& config) override
		{
			if (m_initialized)
			{
				return true;
			}
			if (!window || !graphics_context || !render_device)
			{
				return false;
			}

			m_window = window;
			m_graphics_context = graphics_context;
			m_render_device = render_device;
			m_backend = Application::get_rhi_backend();
			m_glfw_window = static_cast<GLFWwindow*>(window->get_native_interface());

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags = ImGuiConfigFlags_None;
			if (config.enable_keyboard_navigation)
			{
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			}
			if (config.enable_gamepad_navigation)
			{
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			}
			if (config.enable_docking)
			{
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			}
			if (config.enable_viewports)
			{
				HLogWarning("UIContext requested multi-viewport support, but the engine UI facade currently keeps it disabled.");
			}
			io.IniFilename = config.ini_path;
			ImGui::StyleColorsDark();

			const bool glfw_ok =
				m_backend == RHI::Backend::Vulkan ?
				ImGui_ImplGlfw_InitForVulkan(m_glfw_window, false) :
				ImGui_ImplGlfw_InitForOther(m_glfw_window, false);
			if (!glfw_ok)
			{
				ImGui::DestroyContext();
				reset_state();
				return false;
			}

			bool backend_ok = false;
			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				backend_ok = init_vulkan_backend();
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				backend_ok = init_dx12_backend();
				break;
#endif
			default:
				break;
			}

			if (!backend_ok)
			{
#if defined(ASH_HAS_VULKAN)
				if (m_vk_descriptor_pool != VK_NULL_HANDLE)
				{
					vkDestroyDescriptorPool(RHI::VulkanContext::get_vulkan_device(), m_vk_descriptor_pool, RHI::VulkanContext::get_vulkan_allocation_callbacks());
					m_vk_descriptor_pool = VK_NULL_HANDLE;
				}
				m_vk_render_pass.reset();
				m_vk_sampler = VK_NULL_HANDLE;
#endif
#if defined(ASH_HAS_DX12)
				m_dx12_srv_heap.Reset();
				m_dx12_free_descriptor_indices.clear();
				m_dx12_descriptor_size = 0u;
				m_dx12_descriptor_capacity = 0u;
				m_dx12_next_descriptor_index = 1u;
				m_dx12_heap_cpu_start = {};
				m_dx12_heap_gpu_start = {};
#endif
				ImGui_ImplGlfw_Shutdown();
				ImGui::DestroyContext();
				reset_state();
				return false;
			}

			m_initialized = true;
			return true;
		}

		void shutdown() override
		{
			if (!m_initialized && !ImGui::GetCurrentContext())
			{
				reset_state();
				return;
			}

			if (m_frame_active && ImGui::GetCurrentContext())
			{
				ImGui::EndFrame();
				m_frame_active = false;
			}

			clear_texture_registrations();

			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				ImGui_ImplVulkan_Shutdown();
				if (m_vk_descriptor_pool != VK_NULL_HANDLE)
				{
					vkDestroyDescriptorPool(RHI::VulkanContext::get_vulkan_device(), m_vk_descriptor_pool, RHI::VulkanContext::get_vulkan_allocation_callbacks());
					m_vk_descriptor_pool = VK_NULL_HANDLE;
				}
				m_vk_render_pass.reset();
				m_vk_sampler = VK_NULL_HANDLE;
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				ImGui_ImplDX12_Shutdown();
				m_dx12_srv_heap.Reset();
				m_dx12_free_descriptor_indices.clear();
				m_dx12_descriptor_size = 0;
				m_dx12_descriptor_capacity = 0;
				m_dx12_next_descriptor_index = 1;
				m_dx12_heap_cpu_start = {};
				m_dx12_heap_gpu_start = {};
				break;
#endif
			default:
				break;
			}

			ImGui_ImplGlfw_Shutdown();
			if (ImGui::GetCurrentContext())
			{
				ImGui::DestroyContext();
			}
			reset_state();
		}

		bool is_initialized() const override
		{
			return m_initialized;
		}

		bool begin_frame() override
		{
			if (!m_initialized)
			{
				return false;
			}

			cleanup_dead_registrations();

			if (m_frame_active && ImGui::GetCurrentContext())
			{
				ImGui::EndFrame();
				m_frame_active = false;
			}

#if defined(ASH_HAS_VULKAN)
			if (m_backend == RHI::Backend::Vulkan)
			{
				const uint32_t current_image_count = get_imgui_image_count();
				if (current_image_count > 0 && current_image_count != m_vk_image_count)
				{
					ImGui_ImplVulkan_SetMinImageCount(current_image_count);
					m_vk_image_count = current_image_count;
				}
				ImGui_ImplVulkan_NewFrame();
			}
#endif
#if defined(ASH_HAS_DX12)
			if (m_backend == RHI::Backend::DirectX12)
			{
				ImGui_ImplDX12_NewFrame();
			}
#endif
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			m_frame_active = true;
			return true;
		}

		bool render(const std::vector<std::shared_ptr<RenderTarget>>& sampled_render_targets) override
		{
			if (!m_initialized)
			{
				return false;
			}
			if (!m_frame_active)
			{
				return true;
			}

			ImGui::Render();
			m_frame_active = false;

			ImDrawData* draw_data = ImGui::GetDrawData();
			if (!draw_data || draw_data->TotalVtxCount == 0 || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
			{
				return true;
			}
			if (!m_render_device || !m_render_device->get_current_command_buffer())
			{
				return false;
			}

			for (const auto& render_target : sampled_render_targets)
			{
				if (!render_target)
				{
					continue;
				}
				if (!m_render_device->transition_render_target_for_sampling(render_target))
				{
					HLogWarning("UIContext skipped a render target transition before UI composition.");
				}
			}

			PassDesc pass_desc{};
			pass_desc.name = "EngineImGuiOverlayPass";
			pass_desc.color_attachments.resize(1);
			pass_desc.color_attachments[0].render_target = m_render_device->get_back_buffer();
			pass_desc.color_attachments[0].load_action =
				m_render_device->has_back_buffer_content() ?
				RenderLoadAction::Load :
				RenderLoadAction::Clear;
			pass_desc.color_attachments[0].clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
			if (!m_render_device->begin_pass(pass_desc))
			{
				return false;
			}

			bool result = false;
			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				result = render_vulkan(draw_data);
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				result = render_dx12(draw_data);
				break;
#endif
			default:
				break;
			}

			m_render_device->end_pass();
			return result;
		}

		bool is_frame_active() const override
		{
			return m_frame_active;
		}

		void handle_window_event(const WindowEvent& event) override
		{
			if (!m_initialized || !m_glfw_window)
			{
				return;
			}

			switch (event.type)
			{
			case WindowEventType::KeyPressed:
				ImGui_ImplGlfw_KeyCallback(m_glfw_window, event.key, event.scancode, event.repeated ? GLFW_REPEAT : GLFW_PRESS, event.mods);
				break;
			case WindowEventType::KeyReleased:
				ImGui_ImplGlfw_KeyCallback(m_glfw_window, event.key, event.scancode, GLFW_RELEASE, event.mods);
				break;
			case WindowEventType::TextInput:
				ImGui_ImplGlfw_CharCallback(m_glfw_window, event.codepoint);
				break;
			case WindowEventType::MouseButtonPressed:
				ImGui_ImplGlfw_MouseButtonCallback(m_glfw_window, event.mouseButton, GLFW_PRESS, event.mods);
				break;
			case WindowEventType::MouseButtonReleased:
				ImGui_ImplGlfw_MouseButtonCallback(m_glfw_window, event.mouseButton, GLFW_RELEASE, event.mods);
				break;
			case WindowEventType::MouseMoved:
				ImGui_ImplGlfw_CursorPosCallback(m_glfw_window, event.mouseX, event.mouseY);
				break;
			case WindowEventType::MouseScrolled:
				ImGui_ImplGlfw_ScrollCallback(m_glfw_window, event.scrollX, event.scrollY);
				break;
			case WindowEventType::None:
			case WindowEventType::Resize:
			case WindowEventType::Minimized:
			case WindowEventType::Restored:
			case WindowEventType::CloseRequested:
			default:
				break;
			}
		}

		UITextureHandle register_render_target(const std::shared_ptr<RenderTarget>& render_target) override
		{
			return get_render_target_texture_id(render_target);
		}

		void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target) override
		{
			if (!render_target)
			{
				return;
			}

			auto it = m_texture_registrations.find(render_target.get());
			if (it == m_texture_registrations.end())
			{
				return;
			}

			release_registration(it->second);
			m_texture_registrations.erase(it);
		}

		UITextureHandle get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target) override
		{
			if (!m_initialized || !render_target)
			{
				return nullptr;
			}

			cleanup_dead_registrations();

			TextureRegistration& registration = m_texture_registrations[render_target.get()];
			registration.render_target = render_target;

			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				return ensure_vulkan_registration(registration, render_target);
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				return ensure_dx12_registration(registration, render_target);
#endif
			default:
				break;
			}

			return nullptr;
		}

		bool wants_capture_mouse() const override
		{
			return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
		}

		bool wants_capture_keyboard() const override
		{
			return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
		}

		bool wants_text_input() const override
		{
			return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;
		}

	private:
		struct TextureRegistration
		{
			std::weak_ptr<RenderTarget> render_target;
			UITextureHandle texture_id = nullptr;
#if defined(ASH_HAS_VULKAN)
			VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
			VkImageView image_view = VK_NULL_HANDLE;
#endif
#if defined(ASH_HAS_DX12)
			uint32_t descriptor_index = UINT32_MAX;
			D3D12_CPU_DESCRIPTOR_HANDLE source_cpu_handle{};
			D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
#endif
		};

	private:
		void reset_state()
		{
			m_window = nullptr;
			m_glfw_window = nullptr;
			m_graphics_context = nullptr;
			m_render_device = nullptr;
			m_initialized = false;
			m_frame_active = false;
			m_backend = RHI::Backend::Default;
			m_texture_registrations.clear();
		}

		void cleanup_dead_registrations()
		{
			for (auto it = m_texture_registrations.begin(); it != m_texture_registrations.end(); )
			{
				if (it->second.render_target.expired())
				{
					release_registration(it->second);
					it = m_texture_registrations.erase(it);
					continue;
				}
				++it;
			}
		}

		void clear_texture_registrations()
		{
			for (auto& [key, registration] : m_texture_registrations)
			{
				(void)key;
				release_registration(registration, true);
			}
			m_texture_registrations.clear();
		}

		void release_registration(TextureRegistration& registration, bool immediate = false)
		{
#if defined(ASH_HAS_VULKAN)
			if (registration.descriptor_set != VK_NULL_HANDLE)
			{
				const VkDescriptorSet descriptor_set = registration.descriptor_set;
				if (!immediate && m_backend == RHI::Backend::Vulkan && RHI::VulkanContext::get_current_frame() != UINT32_MAX)
				{
					RHI::VulkanContext::get_current_frame_deletion_queue().emplace([descriptor_set]() {
						ImGui_ImplVulkan_RemoveTexture(descriptor_set);
					});
				}
				else
				{
					ImGui_ImplVulkan_RemoveTexture(descriptor_set);
				}
				registration.descriptor_set = VK_NULL_HANDLE;
				registration.image_view = VK_NULL_HANDLE;
			}
#endif
#if defined(ASH_HAS_DX12)
			if (registration.descriptor_index != UINT32_MAX)
			{
				if (registration.descriptor_index != 0u)
				{
					m_dx12_free_descriptor_indices.push_back(registration.descriptor_index);
				}
				registration.descriptor_index = UINT32_MAX;
				registration.source_cpu_handle = {};
				registration.gpu_handle = {};
			}
#endif
			registration.texture_id = nullptr;
			registration.render_target.reset();
		}

		auto get_imgui_image_count() const -> uint32_t
		{
			if (!Application::get_swapchain())
			{
				return 2u;
			}
			return std::max<uint32_t>(2u, Application::get_swapchain()->get_swapchain_buffer_count());
		}

#if defined(ASH_HAS_VULKAN)
		bool init_vulkan_backend()
		{
			auto* vulkan_context = static_cast<RHI::VulkanContext*>(m_graphics_context);
			if (!vulkan_context)
			{
				return false;
			}

			const std::shared_ptr<RenderTarget> back_buffer = m_render_device->get_back_buffer();
			if (!back_buffer)
			{
				return false;
			}

			const VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048u };
			VkDescriptorPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			pool_info.maxSets = 2048u;
			pool_info.poolSizeCount = 1u;
			pool_info.pPoolSizes = &pool_size;
			if (vkCreateDescriptorPool(RHI::VulkanContext::get_vulkan_device(), &pool_info, RHI::VulkanContext::get_vulkan_allocation_callbacks(), &m_vk_descriptor_pool) != VK_SUCCESS)
			{
				return false;
			}

			const RHI::AshFormat back_buffer_format = to_rhi_format(back_buffer->get_format());
			const VkFormat vk_color_attachment_format = RHI::get_vk_texture_format_info(back_buffer_format).vkFormat;
			if (vk_color_attachment_format == VK_FORMAT_UNDEFINED)
			{
				return false;
			}

			std::shared_ptr<RHI::Sampler> sampler = m_graphics_context->get_sampler(RHI::ASH_SAMPLER_STATE_DEFAULT);
			if (!sampler)
			{
				return false;
			}
			m_vk_sampler = static_cast<RHI::VulkanSampler*>(sampler.get())->get_vk_sampler();

			ImGui_ImplVulkan_LoadFunctions(
				[](const char* function_name, void* user_data) -> PFN_vkVoidFunction
				{
					(void)user_data;
					return vkGetInstanceProcAddr(RHI::VulkanContext::get_vulkan_instance(), function_name);
				},
				nullptr);

			m_vk_image_count = get_imgui_image_count();

			ImGui_ImplVulkan_InitInfo init_info{};
			init_info.Instance = RHI::VulkanContext::get_vulkan_instance();
			init_info.PhysicalDevice = RHI::VulkanContext::get_vulkan_physical_device();
			init_info.Device = RHI::VulkanContext::get_vulkan_device();
			init_info.QueueFamily = RHI::VulkanContext::get_queue_family_index(RHI::AshQueueType::Graphics);
			init_info.Queue = RHI::VulkanContext::get_graphics_queue();
			init_info.PipelineCache = RHI::VulkanContext::get_pipeline_cache();
			init_info.DescriptorPool = m_vk_descriptor_pool;
			init_info.Subpass = 0u;
			init_info.MinImageCount = m_vk_image_count;
			init_info.ImageCount = m_vk_image_count;
			init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			init_info.Allocator = RHI::VulkanContext::get_vulkan_allocation_callbacks();
			init_info.CheckVkResultFn = check_imgui_vk_result;
			const bool use_dynamic_rendering = vulkan_context->get_device_extension_enabled(RHI::DeviceExtensionAndFeaturesFlags::DynamicRendering);
			init_info.UseDynamicRendering = use_dynamic_rendering;
			if (use_dynamic_rendering)
			{
				init_info.ColorAttachmentFormat = vk_color_attachment_format;
				return ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);
			}

			RHI::RenderPassCreation render_pass_creation{};
			render_pass_creation.set_name("EngineImGuiRenderPass");
			render_pass_creation.add_attachment(back_buffer_format, RHI::AshResourceState::Present, RHI::AshLoadOption::ASH_LOAD_LOAD);
			m_vk_render_pass = m_graphics_context->create_render_pass(render_pass_creation);
			if (!m_vk_render_pass)
			{
				return false;
			}

			return ImGui_ImplVulkan_Init(&init_info, reinterpret_cast<VkRenderPass>(m_vk_render_pass->get_native_handle()));
		}

		bool render_vulkan(ImDrawData* draw_data)
		{
			auto* command_buffer = static_cast<RHI::VulkanCommandBuffer*>(m_render_device->get_current_command_buffer());
			if (!command_buffer)
			{
				return false;
			}

			ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer->get_vkCommandBuffer());
			return true;
		}

		UITextureHandle ensure_vulkan_registration(TextureRegistration& registration, const std::shared_ptr<RenderTarget>& render_target)
		{
			std::shared_ptr<RHI::TextureView> shader_resource_view = m_render_device->get_shader_resource_view(render_target);
			auto* vulkan_texture_view = shader_resource_view ? static_cast<RHI::VulkanTextureView*>(shader_resource_view.get()) : nullptr;
			const VkImageView image_view = vulkan_texture_view ? vulkan_texture_view->get_vk_image_view() : VK_NULL_HANDLE;
			if (image_view == VK_NULL_HANDLE || m_vk_sampler == VK_NULL_HANDLE)
			{
				return nullptr;
			}

			if (registration.descriptor_set != VK_NULL_HANDLE && registration.image_view == image_view)
			{
				return registration.texture_id;
			}

			if (registration.descriptor_set != VK_NULL_HANDLE)
			{
				const VkDescriptorSet old_descriptor_set = registration.descriptor_set;
				if (RHI::VulkanContext::get_current_frame() != UINT32_MAX)
				{
					RHI::VulkanContext::get_current_frame_deletion_queue().emplace([old_descriptor_set]() {
						ImGui_ImplVulkan_RemoveTexture(old_descriptor_set);
					});
				}
				else
				{
					ImGui_ImplVulkan_RemoveTexture(old_descriptor_set);
				}
				registration.descriptor_set = VK_NULL_HANDLE;
				registration.image_view = VK_NULL_HANDLE;
			}

			registration.descriptor_set = ImGui_ImplVulkan_AddTexture(m_vk_sampler, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			registration.image_view = image_view;
			registration.texture_id = reinterpret_cast<UITextureHandle>(registration.descriptor_set);
			return registration.texture_id;
		}
#endif

#if defined(ASH_HAS_DX12)
		bool init_dx12_backend()
		{
			auto* dx12_context = static_cast<RHI::DX12Context*>(m_graphics_context);
			if (!dx12_context)
			{
				return false;
			}

			const std::shared_ptr<RenderTarget> back_buffer = m_render_device->get_back_buffer();
			if (!back_buffer)
			{
				return false;
			}

			m_dx12_descriptor_capacity = 2048u;
			m_dx12_next_descriptor_index = 1u;
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.NumDescriptors = m_dx12_descriptor_capacity;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heap_desc.NodeMask = 0u;
			if (FAILED(dx12_context->get_device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_dx12_srv_heap))))
			{
				return false;
			}

			m_dx12_descriptor_size = dx12_context->get_device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_dx12_heap_cpu_start = m_dx12_srv_heap->GetCPUDescriptorHandleForHeapStart();
			m_dx12_heap_gpu_start = m_dx12_srv_heap->GetGPUDescriptorHandleForHeapStart();

			const DXGI_FORMAT rtv_format = RHI::ash_to_dxgi_format(to_rhi_format(back_buffer->get_format()));
			return ImGui_ImplDX12_Init(
				dx12_context->get_device(),
				static_cast<int>(get_imgui_image_count()),
				rtv_format,
				m_dx12_srv_heap.Get(),
				m_dx12_heap_cpu_start,
				m_dx12_heap_gpu_start);
		}

		bool render_dx12(ImDrawData* draw_data)
		{
			auto* command_buffer = static_cast<RHI::DX12CommandBuffer*>(m_render_device->get_current_command_buffer());
			if (!command_buffer || !m_dx12_srv_heap)
			{
				return false;
			}

			ID3D12DescriptorHeap* descriptor_heaps[] = { m_dx12_srv_heap.Get() };
			command_buffer->get_command_list()->SetDescriptorHeaps(1u, descriptor_heaps);
			ImGui_ImplDX12_RenderDrawData(draw_data, command_buffer->get_command_list());
			return true;
		}

		auto make_dx12_cpu_handle(uint32_t descriptor_index) const -> D3D12_CPU_DESCRIPTOR_HANDLE
		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle = m_dx12_heap_cpu_start;
			handle.ptr += static_cast<SIZE_T>(descriptor_index) * m_dx12_descriptor_size;
			return handle;
		}

		auto make_dx12_gpu_handle(uint32_t descriptor_index) const -> D3D12_GPU_DESCRIPTOR_HANDLE
		{
			D3D12_GPU_DESCRIPTOR_HANDLE handle = m_dx12_heap_gpu_start;
			handle.ptr += static_cast<UINT64>(descriptor_index) * static_cast<UINT64>(m_dx12_descriptor_size);
			return handle;
		}

		auto allocate_dx12_descriptor_index() -> uint32_t
		{
			if (!m_dx12_free_descriptor_indices.empty())
			{
				const uint32_t descriptor_index = m_dx12_free_descriptor_indices.back();
				m_dx12_free_descriptor_indices.pop_back();
				return descriptor_index;
			}
			if (m_dx12_next_descriptor_index >= m_dx12_descriptor_capacity)
			{
				HLogError("ImGuiLayer DX12 descriptor heap exhausted.");
				return UINT32_MAX;
			}
			return m_dx12_next_descriptor_index++;
		}

		UITextureHandle ensure_dx12_registration(TextureRegistration& registration, const std::shared_ptr<RenderTarget>& render_target)
		{
			auto* dx12_context = static_cast<RHI::DX12Context*>(m_graphics_context);
			if (!dx12_context || !m_dx12_srv_heap)
			{
				return nullptr;
			}

			std::shared_ptr<RHI::TextureView> shader_resource_view = m_render_device->get_shader_resource_view(render_target);
			auto* dx12_texture_view = shader_resource_view ? static_cast<RHI::DX12TextureView*>(shader_resource_view.get()) : nullptr;
			if (!dx12_texture_view)
			{
				return nullptr;
			}

			const D3D12_CPU_DESCRIPTOR_HANDLE source_cpu_handle = dx12_texture_view->get_descriptor_handle().cpuHandle;
			if (registration.descriptor_index == UINT32_MAX)
			{
				registration.descriptor_index = allocate_dx12_descriptor_index();
				if (registration.descriptor_index == UINT32_MAX)
				{
					return nullptr;
				}
				registration.gpu_handle = make_dx12_gpu_handle(registration.descriptor_index);
				registration.texture_id = reinterpret_cast<UITextureHandle>(registration.gpu_handle.ptr);
			}

			const D3D12_CPU_DESCRIPTOR_HANDLE destination_cpu_handle = make_dx12_cpu_handle(registration.descriptor_index);
			dx12_context->get_device()->CopyDescriptorsSimple(1u, destination_cpu_handle, source_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			registration.source_cpu_handle = source_cpu_handle;

			return registration.texture_id;
		}
#endif

	private:
		Window* m_window = nullptr;
		GLFWwindow* m_glfw_window = nullptr;
		RHI::GraphicsContext* m_graphics_context = nullptr;
		RenderDevice* m_render_device = nullptr;
		RHI::Backend m_backend = RHI::Backend::Default;
		bool m_initialized = false;
		bool m_frame_active = false;
		std::unordered_map<const RenderTarget*, TextureRegistration> m_texture_registrations{};

#if defined(ASH_HAS_VULKAN)
		VkDescriptorPool m_vk_descriptor_pool = VK_NULL_HANDLE;
		VkSampler m_vk_sampler = VK_NULL_HANDLE;
		std::shared_ptr<RHI::RenderPass> m_vk_render_pass = nullptr;
		uint32_t m_vk_image_count = 0u;
#endif

#if defined(ASH_HAS_DX12)
		ComPtr<ID3D12DescriptorHeap> m_dx12_srv_heap;
		uint32_t m_dx12_descriptor_size = 0u;
		uint32_t m_dx12_descriptor_capacity = 0u;
		uint32_t m_dx12_next_descriptor_index = 1u;
		std::vector<uint32_t> m_dx12_free_descriptor_indices{};
		D3D12_CPU_DESCRIPTOR_HANDLE m_dx12_heap_cpu_start{};
		D3D12_GPU_DESCRIPTOR_HANDLE m_dx12_heap_gpu_start{};
#endif
	};

	auto create_imgui_layer(RHI::Backend backend) -> std::unique_ptr<ImGuiLayer>
	{
		switch (backend)
		{
#if defined(ASH_HAS_VULKAN)
		case RHI::Backend::Vulkan:
#endif
#if defined(ASH_HAS_DX12)
		case RHI::Backend::DirectX12:
#endif
			return std::make_unique<NativeImGuiLayer>();
		default:
			return nullptr;
		}
	}
}
