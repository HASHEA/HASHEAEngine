#include "ImGui/EditorImGuiLayer.h"
#include "ImGui/EditorStyle.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"

namespace AshEditor
{
	bool EditorImGuiLayer::init(void* native_window, const std::filesystem::path& ini_file_path)
	{
		if (m_initialized)
		{
			return true;
		}

		if (!native_window)
		{
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		std::filesystem::create_directories(ini_file_path.parent_path());
		m_iniFilePath = ini_file_path.string();
		io.IniFilename = m_iniFilePath.c_str();

		EditorStyle::apply();

		if (!ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(native_window), true))
		{
			ImGui::DestroyContext();
			return false;
		}

		m_initialized = true;
		m_rendererBackendReady = false;
		return true;
	}

	void EditorImGuiLayer::shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		m_initialized = false;
		m_rendererBackendReady = false;
		m_iniFilePath.clear();
	}

	bool EditorImGuiLayer::begin_frame()
	{
		if (!m_initialized)
		{
			return false;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		return true;
	}

	void EditorImGuiLayer::render()
	{
		if (!m_initialized)
		{
			return;
		}

		ImGui::Render();
	}

	bool EditorImGuiLayer::is_initialized() const
	{
		return m_initialized;
	}

	bool EditorImGuiLayer::has_renderer_backend() const
	{
		return m_rendererBackendReady;
	}
}
