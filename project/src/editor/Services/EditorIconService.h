#pragma once

#include "Services/IEditorIconService.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace AshEngine
{
	class UIContext;
	class UITexture;
}

namespace AshEditor
{
	class EditorIconService final : public IEditorIconService
	{
	public:
		// Initializes icon paths relative to the workspace root.
		bool Initialize(const std::filesystem::path& refWorkspaceRoot) override;

		// Releases any registered UI handles. If pUiContext is provided and matches the registered one, handles are unregistered.
		void Shutdown(AshEngine::UIContext* pUiContext = nullptr) override;

		// Returns a UI texture handle for the given icon id. May return nullptr if the icon failed to load.
		AshEngine::UITextureHandle GetIcon(EditorIconId eIconId, AshEngine::UIContext& refUiContext) override;

	private:
		struct IconEntry
		{
			std::filesystem::path pathFile{};
			std::string strDebugName{};
			std::shared_ptr<AshEngine::UITexture> spUiTexture = nullptr;
			AshEngine::UITextureHandle pUiTextureHandle = nullptr;
			bool bLoadFailed = false;
		};

		void RegisterDefaultIcons();
		void ClearHandles();
		bool EnsureIconLoaded(IconEntry& refEntry, AshEngine::UIContext& refUiContext);
		IconEntry& GetEntry(EditorIconId eIconId);

	private:
		std::filesystem::path _pathWorkspaceRoot{};
		std::filesystem::path _pathIconRoot{};
		AshEngine::UIContext* _pRegisteredUiContext = nullptr;
		std::array<IconEntry, static_cast<size_t>(EditorIconId::Count)> _arrIcons{};
	};
}
