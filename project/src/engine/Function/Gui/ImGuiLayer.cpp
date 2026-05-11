#include "ImGuiLayer.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderFormatUtils.h"
#include "Graphics/RenderPass.h"
#include "Graphics/Swapchain.h"
#include "Graphics/Texture.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "GLFW/glfw3.h"
#include <mutex>

#if defined(ASH_HAS_VULKAN)
#include "Graphics/Vulkan/VulkanCommandBuffer.h"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanHelper.hpp"
#include "Graphics/Vulkan/VulkanRenderPass.h"
#include "Graphics/Vulkan/VulkanSampler.h"
#include "Graphics/Vulkan/VulkanTexture.h"
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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static auto make_color(float r, float g, float b, float a = 1.0f) -> ImVec4
		{
			return ImVec4(r, g, b, a);
		}

		static void apply_classic_dark_theme()
		{
			ImGui::StyleColorsDark();
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowPadding = ImVec2(10.0f, 8.0f);
			style.FramePadding = ImVec2(8.0f, 6.0f);
			style.ItemSpacing = ImVec2(8.0f, 6.0f);
			style.WindowRounding = 6.0f;
			style.ChildRounding = 5.0f;
			style.PopupRounding = 5.0f;
			style.FrameRounding = 4.0f;
			style.TabRounding = 4.0f;
			style.WindowBorderSize = 1.0f;
			style.ChildBorderSize = 1.0f;
			style.PopupBorderSize = 1.0f;
			style.FrameBorderSize = 1.0f;
		}

		static void apply_slate_studio_theme()
		{
			ImGui::StyleColorsDark();

			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowPadding = ImVec2(12.0f, 10.0f);
			style.FramePadding = ImVec2(10.0f, 6.0f);
			style.CellPadding = ImVec2(8.0f, 6.0f);
			style.ItemSpacing = ImVec2(10.0f, 8.0f);
			style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
			style.IndentSpacing = 20.0f;
			style.ScrollbarSize = 13.0f;
			style.GrabMinSize = 10.0f;

			style.WindowRounding = 7.0f;
			style.ChildRounding = 6.0f;
			style.PopupRounding = 6.0f;
			style.FrameRounding = 5.0f;
			style.ScrollbarRounding = 9.0f;
			style.GrabRounding = 4.0f;
			style.TabRounding = 5.0f;
			style.TabBorderSize = 1.0f;
			style.TabBarBorderSize = 1.0f;

			style.WindowBorderSize = 1.0f;
			style.ChildBorderSize = 1.0f;
			style.PopupBorderSize = 1.0f;
			style.FrameBorderSize = 1.0f;
			style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

			ImVec4* colors = style.Colors;
			colors[ImGuiCol_Text] = make_color(0.90f, 0.92f, 0.95f);
			colors[ImGuiCol_TextDisabled] = make_color(0.60f, 0.64f, 0.70f);
			colors[ImGuiCol_WindowBg] = make_color(0.07f, 0.09f, 0.11f);
			colors[ImGuiCol_ChildBg] = make_color(0.09f, 0.11f, 0.14f);
			colors[ImGuiCol_PopupBg] = make_color(0.10f, 0.13f, 0.16f, 0.98f);
			colors[ImGuiCol_Border] = make_color(0.16f, 0.20f, 0.25f, 0.95f);
			colors[ImGuiCol_BorderShadow] = make_color(0.00f, 0.00f, 0.00f, 0.00f);

			colors[ImGuiCol_FrameBg] = make_color(0.12f, 0.15f, 0.19f);
			colors[ImGuiCol_FrameBgHovered] = make_color(0.20f, 0.26f, 0.33f);
			colors[ImGuiCol_FrameBgActive] = make_color(0.27f, 0.35f, 0.43f);

			colors[ImGuiCol_TitleBg] = make_color(0.08f, 0.10f, 0.13f);
			colors[ImGuiCol_TitleBgActive] = make_color(0.11f, 0.14f, 0.18f);
			colors[ImGuiCol_TitleBgCollapsed] = make_color(0.08f, 0.10f, 0.13f, 0.80f);
			colors[ImGuiCol_MenuBarBg] = make_color(0.09f, 0.11f, 0.14f);

			colors[ImGuiCol_ScrollbarBg] = make_color(0.07f, 0.09f, 0.12f);
			colors[ImGuiCol_ScrollbarGrab] = make_color(0.21f, 0.26f, 0.32f);
			colors[ImGuiCol_ScrollbarGrabHovered] = make_color(0.27f, 0.33f, 0.41f);
			colors[ImGuiCol_ScrollbarGrabActive] = make_color(0.31f, 0.39f, 0.48f);

			colors[ImGuiCol_CheckMark] = make_color(0.47f, 0.67f, 0.88f);
			colors[ImGuiCol_SliderGrab] = make_color(0.39f, 0.58f, 0.78f);
			colors[ImGuiCol_SliderGrabActive] = make_color(0.49f, 0.70f, 0.92f);

			colors[ImGuiCol_Button] = make_color(0.18f, 0.25f, 0.32f);
			colors[ImGuiCol_ButtonHovered] = make_color(0.29f, 0.40f, 0.51f);
			colors[ImGuiCol_ButtonActive] = make_color(0.37f, 0.52f, 0.65f);

			colors[ImGuiCol_Header] = make_color(0.20f, 0.28f, 0.36f);
			colors[ImGuiCol_HeaderHovered] = make_color(0.31f, 0.44f, 0.56f);
			colors[ImGuiCol_HeaderActive] = make_color(0.39f, 0.55f, 0.70f);

			colors[ImGuiCol_Separator] = make_color(0.18f, 0.23f, 0.29f);
			colors[ImGuiCol_SeparatorHovered] = make_color(0.31f, 0.42f, 0.53f);
			colors[ImGuiCol_SeparatorActive] = make_color(0.40f, 0.54f, 0.67f);

			colors[ImGuiCol_ResizeGrip] = make_color(0.22f, 0.31f, 0.39f, 0.35f);
			colors[ImGuiCol_ResizeGripHovered] = make_color(0.35f, 0.49f, 0.61f, 0.80f);
			colors[ImGuiCol_ResizeGripActive] = make_color(0.44f, 0.61f, 0.76f, 0.95f);

			colors[ImGuiCol_Tab] = make_color(0.12f, 0.16f, 0.20f);
			colors[ImGuiCol_TabHovered] = make_color(0.26f, 0.38f, 0.49f);
			colors[ImGuiCol_TabActive] = make_color(0.34f, 0.49f, 0.62f);
			colors[ImGuiCol_TabUnfocused] = make_color(0.09f, 0.12f, 0.15f);
			colors[ImGuiCol_TabUnfocusedActive] = make_color(0.23f, 0.33f, 0.42f);
			colors[ImGuiCol_DockingPreview] = make_color(0.34f, 0.55f, 0.79f, 0.70f);
			colors[ImGuiCol_DockingEmptyBg] = make_color(0.06f, 0.08f, 0.10f);

			colors[ImGuiCol_TableHeaderBg] = make_color(0.11f, 0.14f, 0.18f);
			colors[ImGuiCol_TableBorderStrong] = make_color(0.19f, 0.24f, 0.30f);
			colors[ImGuiCol_TableBorderLight] = make_color(0.13f, 0.17f, 0.21f);
			colors[ImGuiCol_TableRowBg] = make_color(0.00f, 0.00f, 0.00f, 0.00f);
			colors[ImGuiCol_TableRowBgAlt] = make_color(1.00f, 1.00f, 1.00f, 0.03f);

			colors[ImGuiCol_TextSelectedBg] = make_color(0.29f, 0.45f, 0.64f, 0.35f);
			colors[ImGuiCol_DragDropTarget] = make_color(0.51f, 0.75f, 0.96f, 0.90f);
			colors[ImGuiCol_NavHighlight] = make_color(0.41f, 0.61f, 0.83f, 0.85f);
			colors[ImGuiCol_NavWindowingHighlight] = make_color(1.00f, 1.00f, 1.00f, 0.70f);
		}

		static void apply_imgui_theme_preset(UIThemePreset preset)
		{
			switch (preset)
			{
			case UIThemePreset::ClassicDark:
				apply_classic_dark_theme();
				break;
			case UIThemePreset::SlateStudio:
			default:
				apply_slate_studio_theme();
				break;
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

		static std::unordered_set<const RenderTarget*> s_logged_missing_texture_prereqs{};
		static std::unordered_set<const RenderTarget*> s_logged_successful_texture_registrations{};
		static std::mutex s_logged_texture_registration_mutex{};
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			if (m_initialized)
			{
				break;
			}
			ASH_PROCESS_ERROR(window && graphics_context && render_device);

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
				// Keep a visible tab handle for single-window dock nodes so editor panels
				// can always be re-docked after users drag them into edge slots.
				io.ConfigDockingAlwaysTabBar = true;
			}
			if (config.enable_viewports)
			{
				HLogWarning("UIContext requested multi-viewport support, but the engine UI facade currently keeps it disabled.");
			}
			m_iniPath = config.ini_path;
			io.IniFilename = m_iniPath.empty() ? nullptr : m_iniPath.c_str();
			io.ConfigWindowsMoveFromTitleBarOnly = true;
			m_themePreset = config.theme_preset;
			apply_imgui_theme_preset(m_themePreset);

			const bool glfw_ok =
				m_backend == RHI::Backend::Vulkan ?
				ImGui_ImplGlfw_InitForVulkan(m_glfw_window, false) :
				ImGui_ImplGlfw_InitForOther(m_glfw_window, false);
			ASH_PROCESS_ERROR(glfw_ok);

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

			ASH_PROCESS_ERROR(backend_ok);

			m_initialized = true;
			ASH_PROCESS_GUARD_END(bResult, false);
			if (!bResult)
			{
				clear_texture_registrations();
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
				if (ImGui::GetCurrentContext())
				{
					ImGui::DestroyContext();
				}
				reset_state();
			}
			return bResult;
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(m_initialized);

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
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		bool render(const std::vector<std::shared_ptr<RenderTarget>>& sampled_render_targets) override
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(m_initialized);
			if (!m_frame_active)
			{
				break;
			}

			ImGui::Render();
			m_frame_active = false;

			ImDrawData* draw_data = ImGui::GetDrawData();
			if (!draw_data || draw_data->TotalVtxCount == 0 || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
			{
				break;
			}
			ASH_PROCESS_ERROR(m_render_device && m_render_device->get_current_command_buffer());

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
			pass_desc.color_attachments[0].clear_color = get_engine_back_buffer_clear_color();
			ASH_PROCESS_ERROR(m_render_device->begin_pass(pass_desc));

			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				bResult = render_vulkan(draw_data);
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				bResult = render_dx12(draw_data);
				break;
#endif
			default:
				bResult = false;
				break;
			}

			bResult = m_render_device->end_pass() && bResult;
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
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
			ASH_PROCESS_GUARD_RETURN(UITextureHandle, texture_id, nullptr, nullptr);
			ASH_PROCESS_ERROR(m_initialized && render_target);

			cleanup_dead_registrations();

			TextureRegistration& registration = m_texture_registrations[render_target.get()];
			registration.render_target = render_target;

			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				texture_id = ensure_vulkan_registration(registration, render_target);
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				texture_id = ensure_dx12_registration(registration, render_target);
				break;
#endif
			default:
				break;
			}

			ASH_PROCESS_GUARD_RETURN_END(texture_id, nullptr);
		}

		UITextureHandle register_texture_view(const std::shared_ptr<RHI::TextureView>& texture_view) override
		{
			return get_texture_view_texture_id(texture_view);
		}

		void unregister_texture_view(const std::shared_ptr<RHI::TextureView>& texture_view) override
		{
			if (!texture_view)
			{
				return;
			}

			auto it = m_texture_view_registrations.find(texture_view.get());
			if (it == m_texture_view_registrations.end())
			{
				return;
			}

			release_registration(it->second);
			m_texture_view_registrations.erase(it);
		}

		UITextureHandle get_texture_view_texture_id(const std::shared_ptr<RHI::TextureView>& texture_view) override
		{
			ASH_PROCESS_GUARD_RETURN(UITextureHandle, texture_id, nullptr, nullptr);
			ASH_PROCESS_ERROR(m_initialized && texture_view);
			ASH_PROCESS_ERROR(texture_view->get_view_type() == RHI::AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV);

			cleanup_dead_registrations();

			TextureRegistration& registration = m_texture_view_registrations[texture_view.get()];
			registration.texture_view = texture_view;
			registration.render_target.reset();

			switch (m_backend)
			{
#if defined(ASH_HAS_VULKAN)
			case RHI::Backend::Vulkan:
				texture_id = ensure_vulkan_registration(registration, {}, texture_view);
				break;
#endif
#if defined(ASH_HAS_DX12)
			case RHI::Backend::DirectX12:
				texture_id = ensure_dx12_registration(registration, texture_view);
				break;
#endif
			default:
				break;
			}

			ASH_PROCESS_GUARD_RETURN_END(texture_id, nullptr);
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

		void apply_theme_preset(UIThemePreset preset) override
		{
			m_themePreset = preset;
			if (ImGui::GetCurrentContext())
			{
				apply_imgui_theme_preset(m_themePreset);
			}
		}

		UIThemePreset get_theme_preset() const override
		{
			return m_themePreset;
		}

	private:
		struct TextureRegistration
		{
			std::weak_ptr<RenderTarget> render_target;
			std::weak_ptr<RHI::TextureView> texture_view;
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

#if defined(ASH_HAS_DX12)
		struct RetiredDx12Descriptor
		{
			uint32_t descriptor_index = UINT32_MAX;
			uint64_t retire_frame = 0u;
		};
#endif

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
			m_texture_view_registrations.clear();
#if defined(ASH_HAS_DX12)
			m_dx12_retired_descriptor_indices.clear();
#endif
		}

		void cleanup_dead_registrations()
		{
#if defined(ASH_HAS_DX12)
			collect_retired_dx12_descriptors();
#endif
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
			for (auto it = m_texture_view_registrations.begin(); it != m_texture_view_registrations.end();)
			{
				if (it->second.texture_view.expired())
				{
					release_registration(it->second);
					it = m_texture_view_registrations.erase(it);
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
			for (auto& [key, registration] : m_texture_view_registrations)
			{
				(void)key;
				release_registration(registration, true);
			}
			m_texture_view_registrations.clear();
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
				retire_dx12_descriptor_index(registration.descriptor_index, immediate);
				registration.descriptor_index = UINT32_MAX;
				registration.source_cpu_handle = {};
				registration.gpu_handle = {};
			}
#endif
			registration.texture_id = nullptr;
			registration.render_target.reset();
			registration.texture_view.reset();
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
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

			auto* vulkan_context = static_cast<RHI::VulkanContext*>(m_graphics_context);
			ASH_PROCESS_ERROR(vulkan_context);

			const std::shared_ptr<RenderTarget> back_buffer = m_render_device->get_back_buffer();
			ASH_PROCESS_ERROR(back_buffer);

			const VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048u };
			VkDescriptorPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			pool_info.maxSets = 2048u;
			pool_info.poolSizeCount = 1u;
			pool_info.pPoolSizes = &pool_size;
			ASH_PROCESS_ERROR(vkCreateDescriptorPool(RHI::VulkanContext::get_vulkan_device(), &pool_info, RHI::VulkanContext::get_vulkan_allocation_callbacks(), &m_vk_descriptor_pool) == VK_SUCCESS);

			const RHI::AshFormat back_buffer_format = render_texture_format_to_rhi(back_buffer->get_format());
			const VkFormat vk_color_attachment_format = RHI::get_vk_texture_format_info(back_buffer_format).vkFormat;
			ASH_PROCESS_ERROR(vk_color_attachment_format != VK_FORMAT_UNDEFINED);

			std::shared_ptr<RHI::Sampler> sampler = m_graphics_context->get_sampler(RHI::ASH_SAMPLER_STATE_DEFAULT);
			ASH_PROCESS_ERROR(sampler);
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
				bResult = ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);
			}
			else
			{
				RHI::RenderPassCreation render_pass_creation{};
				render_pass_creation.set_name("EngineImGuiRenderPass");
				render_pass_creation.add_attachment(back_buffer_format, RHI::AshResourceState::Present, RHI::AshLoadOption::ASH_LOAD_LOAD);
				m_vk_render_pass = m_graphics_context->create_render_pass(render_pass_creation);
				ASH_PROCESS_ERROR(m_vk_render_pass);
				bResult = ImGui_ImplVulkan_Init(&init_info, reinterpret_cast<VkRenderPass>(m_vk_render_pass->get_native_handle()));
			}
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		bool render_vulkan(ImDrawData* draw_data)
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			auto* command_buffer = static_cast<RHI::VulkanCommandBuffer*>(m_render_device->get_current_command_buffer());
			ASH_PROCESS_ERROR(command_buffer);

			ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer->get_vkCommandBuffer());
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		UITextureHandle ensure_vulkan_registration(TextureRegistration& registration, const std::shared_ptr<RenderTarget>& render_target)
		{
			std::shared_ptr<RHI::TextureView> shader_resource_view = m_render_device->get_shader_resource_view(render_target);
			return ensure_vulkan_registration(registration, render_target, shader_resource_view);
		}

		UITextureHandle ensure_vulkan_registration(TextureRegistration& registration, const std::shared_ptr<RenderTarget>& render_target, const std::shared_ptr<RHI::TextureView>& texture_view)
		{
			if (!texture_view || texture_view->get_view_type() != RHI::AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV)
			{
				return nullptr;
			}

			auto* vulkan_texture_view = static_cast<RHI::VulkanTextureView*>(texture_view.get());
			const VkImageView image_view = vulkan_texture_view ? vulkan_texture_view->get_vk_image_view() : VK_NULL_HANDLE;
			if (image_view == VK_NULL_HANDLE || m_vk_sampler == VK_NULL_HANDLE)
			{
				bool should_log_missing_prereqs = false;
				if (render_target)
				{
					std::scoped_lock<std::mutex> lock(s_logged_texture_registration_mutex);
					should_log_missing_prereqs = s_logged_missing_texture_prereqs.insert(render_target.get()).second;
				}
				if (should_log_missing_prereqs)
				{
					HLogError(
						"ImGuiLayer: Vulkan viewport texture registration prerequisites are missing. render_target={}, size={}x{}, format={}, has_srv={}, image_view=0x{:x}, sampler=0x{:x}.",
						static_cast<const void*>(render_target.get()),
						render_target->get_width(),
						render_target->get_height(),
						static_cast<int32_t>(render_target->get_format()),
						texture_view != nullptr,
						static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image_view)),
						static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m_vk_sampler)));
				}
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
			if (registration.descriptor_set == VK_NULL_HANDLE)
			{
				HLogError(
					"ImGuiLayer: ImGui_ImplVulkan_AddTexture returned a null descriptor set for render_target={} image_view=0x{:x}.",
					static_cast<const void*>(render_target.get()),
					static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image_view)));
				return nullptr;
			}
			registration.image_view = image_view;
			registration.texture_id = reinterpret_cast<UITextureHandle>(registration.descriptor_set);
			bool should_log_success = false;
			if (render_target)
			{
				std::scoped_lock<std::mutex> lock(s_logged_texture_registration_mutex);
				should_log_success = s_logged_successful_texture_registrations.insert(render_target.get()).second;
			}
			if (should_log_success)
			{
				HLogInfo(
					"ImGuiLayer: Vulkan viewport texture registered successfully. render_target={}, size={}x{}, descriptor_set=0x{:x}, image_view=0x{:x}.",
					static_cast<const void*>(render_target.get()),
					render_target->get_width(),
					render_target->get_height(),
					static_cast<uint64_t>(reinterpret_cast<uintptr_t>(registration.descriptor_set)),
					static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image_view)));
			}
			return registration.texture_id;
		}
#endif

#if defined(ASH_HAS_DX12)
		bool init_dx12_backend()
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			auto* dx12_context = static_cast<RHI::DX12Context*>(m_graphics_context);
			ASH_PROCESS_ERROR(dx12_context);

			const std::shared_ptr<RenderTarget> back_buffer = m_render_device->get_back_buffer();
			ASH_PROCESS_ERROR(back_buffer);

			m_dx12_descriptor_capacity = 2048u;
			m_dx12_next_descriptor_index = 1u;
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.NumDescriptors = m_dx12_descriptor_capacity;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heap_desc.NodeMask = 0u;
			ASH_PROCESS_ERROR(SUCCEEDED(dx12_context->get_device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_dx12_srv_heap))));

			m_dx12_descriptor_size = dx12_context->get_device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_dx12_heap_cpu_start = m_dx12_srv_heap->GetCPUDescriptorHandleForHeapStart();
			m_dx12_heap_gpu_start = m_dx12_srv_heap->GetGPUDescriptorHandleForHeapStart();

			const DXGI_FORMAT rtv_format = RHI::ash_to_dxgi_format(render_texture_format_to_rhi(back_buffer->get_format()));
			bResult = ImGui_ImplDX12_Init(
				dx12_context->get_device(),
				static_cast<int>(get_imgui_image_count()),
				rtv_format,
				m_dx12_srv_heap.Get(),
				m_dx12_heap_cpu_start,
				m_dx12_heap_gpu_start);
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		bool render_dx12(ImDrawData* draw_data)
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			auto* command_buffer = static_cast<RHI::DX12CommandBuffer*>(m_render_device->get_current_command_buffer());
			ASH_PROCESS_ERROR(command_buffer && m_dx12_srv_heap);

			ID3D12DescriptorHeap* descriptor_heaps[] = { m_dx12_srv_heap.Get() };
			command_buffer->get_command_list()->SetDescriptorHeaps(1u, descriptor_heaps);
			ImGui_ImplDX12_RenderDrawData(draw_data, command_buffer->get_command_list());
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
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
			collect_retired_dx12_descriptors();

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

		void retire_dx12_descriptor_index(uint32_t descriptor_index, bool immediate)
		{
			if (descriptor_index == UINT32_MAX || descriptor_index == 0u)
			{
				return;
			}

			if (immediate || !Application::get())
			{
				m_dx12_free_descriptor_indices.push_back(descriptor_index);
				return;
			}

			m_dx12_retired_descriptor_indices.push_back({
				descriptor_index,
				Application::get()->get_frame_index()
			});
		}

		void collect_retired_dx12_descriptors()
		{
			if (m_dx12_retired_descriptor_indices.empty())
			{
				return;
			}

			const uint64_t current_frame = Application::get() ? Application::get()->get_frame_index() : 0u;
			const uint64_t recycle_latency = static_cast<uint64_t>(std::max<uint32_t>(2u, get_imgui_image_count())) + 1u;
			for (auto it = m_dx12_retired_descriptor_indices.begin(); it != m_dx12_retired_descriptor_indices.end();)
			{
				if (current_frame >= it->retire_frame + recycle_latency)
				{
					m_dx12_free_descriptor_indices.push_back(it->descriptor_index);
					it = m_dx12_retired_descriptor_indices.erase(it);
					continue;
				}
				++it;
			}
		}

		UITextureHandle ensure_dx12_registration(TextureRegistration& registration, const std::shared_ptr<RenderTarget>& render_target)
		{
			auto* dx12_context = static_cast<RHI::DX12Context*>(m_graphics_context);
			if (!dx12_context || !m_dx12_srv_heap)
			{
				return nullptr;
			}

			std::shared_ptr<RHI::TextureView> shader_resource_view = m_render_device->get_shader_resource_view(render_target);
			return ensure_dx12_registration(registration, shader_resource_view);
		}

		UITextureHandle ensure_dx12_registration(TextureRegistration& registration, const std::shared_ptr<RHI::TextureView>& texture_view)
		{
			auto* dx12_context = static_cast<RHI::DX12Context*>(m_graphics_context);
			if (!dx12_context || !m_dx12_srv_heap || !texture_view || texture_view->get_view_type() != RHI::AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV)
			{
				return nullptr;
			}

			auto* dx12_texture_view = static_cast<RHI::DX12TextureView*>(texture_view.get());
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
			else if (registration.source_cpu_handle.ptr == source_cpu_handle.ptr)
			{
				return registration.texture_id;
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
		UIThemePreset m_themePreset = UIThemePreset::SlateStudio;
		std::string m_iniPath{};
		std::unordered_map<const RenderTarget*, TextureRegistration> m_texture_registrations{};
		std::unordered_map<const RHI::TextureView*, TextureRegistration> m_texture_view_registrations{};

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
		std::vector<RetiredDx12Descriptor> m_dx12_retired_descriptor_indices{};
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
