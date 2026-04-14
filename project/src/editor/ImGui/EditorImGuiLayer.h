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

		bool init(void* native_window, const std::filesystem::path& ini_file_path);
		void shutdown();
		bool begin_frame();
		void render();

		bool is_initialized() const;
		bool has_renderer_backend() const;

	private:
		std::string m_iniFilePath{};
		bool m_initialized = false;
		bool m_rendererBackendReady = false;
	};
}
