#pragma once

#include "Function/Gui/UICommon.h"

#include <cstdint>
#include <filesystem>

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

	class IEditorIconService
	{
	public:
		virtual ~IEditorIconService() = default;

		virtual bool Initialize(const std::filesystem::path& refWorkspaceRoot) = 0;
		virtual void Shutdown(AshEngine::UIContext* pUiContext = nullptr) = 0;
		virtual AshEngine::UITextureHandle GetIcon(EditorIconId eIconId, AshEngine::UIContext& refUiContext) = 0;
	};
}
