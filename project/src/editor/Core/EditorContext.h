#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace AshEngine
{
	class UIContext;
	class RenderTarget;
}

namespace AshEditor
{
	class AssetDatabaseService;
	class CommandService;
	class EditorSettingsService;
	class EditorViewportService;
	class SelectionService;
	class SceneService;
	class UndoRedoService;

	struct EditorViewportState
	{
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t requested_width = 0;
		uint32_t requested_height = 0;
		bool focused = false;
		bool hovered = false;
	};

	struct EditorViewportInstance
	{
		std::string id{};
		std::string display_name{};
		EditorViewportState state{};
		std::shared_ptr<AshEngine::RenderTarget> render_target = nullptr;
	};

	struct EditorContext
	{
		SelectionService* selection_service = nullptr;
		SceneService* scene_service = nullptr;
		AssetDatabaseService* asset_database_service = nullptr;
		CommandService* command_service = nullptr;
		UndoRedoService* undo_redo_service = nullptr;
		EditorSettingsService* settings_service = nullptr;
		EditorViewportService* viewport_service = nullptr;
		AshEngine::UIContext* ui_context = nullptr;
		EditorViewportState viewport{};
		bool gui_renderer_ready = false;
	};
}
