// Legacy editor-owned ImGui host.
// Kept only as reference while the runtime editor is hosted by engine UIContext.
// This file is intentionally excluded from premake to avoid creating a second ImGui context.
#include "ImGui/EditorImGuiLayer.h"
#include "ImGui/EditorStyle.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"

namespace AshEditor
{
	bool EditorImGuiLayer::Init(void* pNativeWindow, const std::filesystem::path& pathIniFile)
	{
		if (_bInitialized)
		{
			return true;
		}

		if (!pNativeWindow)
		{
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		std::filesystem::create_directories(pathIniFile.parent_path());
		_strIniFilePath = pathIniFile.string();
		io.IniFilename = _strIniFilePath.c_str();

		EditorStyle::Apply();

		if (!ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(pNativeWindow), true))
		{
			ImGui::DestroyContext();
			return false;
		}

		_bInitialized = true;
		_bRendererBackendReady = false;
		return true;
	}

	void EditorImGuiLayer::Shutdown()
	{
		if (!_bInitialized)
		{
			return;
		}

		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		_bInitialized = false;
		_bRendererBackendReady = false;
		_strIniFilePath.clear();
	}

	bool EditorImGuiLayer::BeginFrame()
	{
		if (!_bInitialized)
		{
			return false;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		return true;
	}

	void EditorImGuiLayer::Render()
	{
		if (!_bInitialized)
		{
			return;
		}

		ImGui::Render();
	}

	bool EditorImGuiLayer::IsInitialized() const
	{
		return _bInitialized;
	}

	bool EditorImGuiLayer::HasRendererBackend() const
	{
		return _bRendererBackendReady;
	}
}
