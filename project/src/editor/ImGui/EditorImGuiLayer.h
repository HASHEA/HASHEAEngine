#pragma once
#include <filesystem>
#include <string>

namespace AshEditor
{
	class EditorImGuiLayer
	{
	public:
		EditorImGuiLayer() = default;
		~EditorImGuiLayer() = default;

		bool Init(void* pNativeWindow, const std::filesystem::path& pathIniFile);
		void Shutdown();
		bool BeginFrame();
		void Render();

		bool IsInitialized() const;
		bool HasRendererBackend() const;

	private:
		std::string _strIniFilePath{};
		bool _bInitialized = false;
		bool _bRendererBackendReady = false;
	};
}
