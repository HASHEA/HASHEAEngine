#pragma once

#include "Function/Gui/UICommon.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace RHI
{
	class Texture;
	class TextureView;
}

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	enum class EditorIconId : uint8_t
	{
		FolderClosed = 0,
		FolderOpen,
		File,
		EntityActor,
		EntityScene,
		EntityCamera,
		EntityLightDirectional,
		EntityLightPoint,
		EntityLightSpot,
		EntityMesh,
		Count
	};

	class EditorIconService
	{
	public:
		bool initialize(const std::filesystem::path& workspace_root);
		void shutdown(AshEngine::UIContext* ui_context = nullptr);

		AshEngine::UITextureHandle get_icon(EditorIconId icon_id, AshEngine::UIContext& ui_context);

	private:
		struct IconEntry
		{
			std::filesystem::path file_path{};
			std::string debug_name{};
			std::shared_ptr<RHI::Texture> texture = nullptr;
			std::shared_ptr<RHI::TextureView> texture_view = nullptr;
			AshEngine::UITextureHandle handle = nullptr;
			bool load_failed = false;
		};

		void register_default_icons();
		void clear_handles();
		bool ensure_icon_loaded(IconEntry& entry);
		IconEntry& get_entry(EditorIconId icon_id);

	private:
		std::filesystem::path m_workspaceRoot{};
		std::filesystem::path m_iconRoot{};
		AshEngine::UIContext* m_registeredUiContext = nullptr;
		std::array<IconEntry, static_cast<size_t>(EditorIconId::Count)> m_icons{};
	};
}
