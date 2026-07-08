#include "Services/EditorIconService.h"

#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Function/Gui/UITexture.h"

#include <stb_image.h>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kEditorIconRoot = "product/assets/editor-temp/unreal-icons";

		struct EditorIconDefinition
		{
			EditorIconId eIconId = EditorIconId::FolderClosed;
			const char* pRelativePath = nullptr;
			const char* pDebugName = nullptr;
		};

		constexpr std::array<EditorIconDefinition, static_cast<size_t>(EditorIconId::Count)> kIconDefinitions{ {
			{ EditorIconId::FolderClosed, "folders/FolderClosed.png", "EditorIcon.FolderClosed" },
			{ EditorIconId::FolderOpen, "folders/FolderOpen.png", "EditorIcon.FolderOpen" },
			{ EditorIconId::File, "files/Default_16x.png", "EditorIcon.File" },
			{ EditorIconId::EntityActor, "files/Actor_16x.png", "EditorIcon.EntityActor" },
			{ EditorIconId::EntityScene, "files/SceneComponent_16x.png", "EditorIcon.EntityScene" },
			{ EditorIconId::EntityCamera, "files/CameraActor_16x.png", "EditorIcon.EntityCamera" },
			{ EditorIconId::EntityLightDirectional, "files/DirectionalLight_16x.png", "EditorIcon.EntityLightDirectional" },
			{ EditorIconId::EntityLightPoint, "files/PointLight_16x.png", "EditorIcon.EntityLightPoint" },
			{ EditorIconId::EntityLightSpot, "files/SpotLight_16x.png", "EditorIcon.EntityLightSpot" },
			{ EditorIconId::EntityMesh, "files/StaticMesh_16x.png", "EditorIcon.EntityMesh" },
		} };

		size_t ToIndex(EditorIconId eIconId)
		{
			return static_cast<size_t>(eIconId);
		}
	}

	bool EditorIconService::Initialize(const std::filesystem::path& refWorkspaceRoot)
	{
		Shutdown();
		_pathWorkspaceRoot = refWorkspaceRoot;
		_pathIconRoot = refWorkspaceRoot / kEditorIconRoot;
		RegisterDefaultIcons();
		return true;
	}

	void EditorIconService::Shutdown(AshEngine::UIContext* pUiContext)
	{
		AshEngine::UIContext* pActiveUiContext = pUiContext ? pUiContext : _pRegisteredUiContext;
		if (pActiveUiContext)
		{
			for (IconEntry& refIcon : _arrIcons)
			{
				if (refIcon.pUiTextureHandle && refIcon.spUiTexture)
				{
					pActiveUiContext->unregister_ui_texture(refIcon.spUiTexture);
				}
			}
		}

		for (IconEntry& refIcon : _arrIcons)
		{
			refIcon = {};
		}

		_pRegisteredUiContext = nullptr;
		_pathIconRoot.clear();
		_pathWorkspaceRoot.clear();
	}

	AshEngine::UITextureHandle EditorIconService::GetIcon(EditorIconId eIconId, AshEngine::UIContext& refUiContext)
	{
		if (_pRegisteredUiContext != &refUiContext)
		{
			ClearHandles();
			_pRegisteredUiContext = &refUiContext;
		}

		IconEntry& refIcon = GetEntry(eIconId);
		if (!EnsureIconLoaded(refIcon, refUiContext))
		{
			return nullptr;
		}

		if (!refIcon.pUiTextureHandle)
		{
			refIcon.pUiTextureHandle = refUiContext.register_ui_texture(refIcon.spUiTexture);
			if (!refIcon.pUiTextureHandle)
			{
				HLogWarning("EditorIconService failed to register icon '{}'.", refIcon.pathFile.generic_string());
			}
		}

		return refIcon.pUiTextureHandle;
	}

	void EditorIconService::RegisterDefaultIcons()
	{
		for (const EditorIconDefinition& refDefinition : kIconDefinitions)
		{
			IconEntry& refIcon = GetEntry(refDefinition.eIconId);
			refIcon.pathFile = _pathIconRoot / refDefinition.pRelativePath;
			refIcon.strDebugName = refDefinition.pDebugName ? refDefinition.pDebugName : "";
		}
	}

	void EditorIconService::ClearHandles()
	{
		for (IconEntry& refIcon : _arrIcons)
		{
			refIcon.pUiTextureHandle = nullptr;
		}
	}

	bool EditorIconService::EnsureIconLoaded(IconEntry& refEntry, AshEngine::UIContext& refUiContext)
	{
		if (refEntry.spUiTexture)
		{
			return true;
		}
		if (refEntry.bLoadFailed)
		{
			return false;
		}
		if (refEntry.pathFile.empty())
		{
			refEntry.bLoadFailed = true;
			return false;
		}
		if (!std::filesystem::exists(refEntry.pathFile))
		{
			HLogWarning("EditorIconService could not find icon '{}'.", refEntry.pathFile.generic_string());
			refEntry.bLoadFailed = true;
			return false;
		}

		int iWidth = 0;
		int iHeight = 0;
		int iChannels = 0;
		stbi_uc* pPixels = stbi_load(refEntry.pathFile.string().c_str(), &iWidth, &iHeight, &iChannels, 4);
		if (!pPixels)
		{
			HLogWarning(
				"EditorIconService failed to decode '{}': {}.",
				refEntry.pathFile.generic_string(),
				stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			refEntry.bLoadFailed = true;
			return false;
		}

		bool bLoaded = false;
		do
		{
			if (iWidth <= 0 || iHeight <= 0 || iWidth > 65535 || iHeight > 65535)
			{
				HLogWarning("EditorIconService rejected '{}' because the image size is invalid ({}x{}).", refEntry.pathFile.generic_string(), iWidth, iHeight);
				break;
			}

			refEntry.spUiTexture = refUiContext.create_ui_texture_rgba8(
				pPixels,
				static_cast<uint32_t>(iWidth),
				static_cast<uint32_t>(iHeight),
				refEntry.strDebugName.c_str());
			if (!refEntry.spUiTexture)
			{
				HLogWarning("EditorIconService failed to create UI texture for '{}'.", refEntry.pathFile.generic_string());
				break;
			}

			bLoaded = true;
		} while (false);

		stbi_image_free(pPixels);

		refEntry.bLoadFailed = !bLoaded;
		return bLoaded;
	}

	EditorIconService::IconEntry& EditorIconService::GetEntry(EditorIconId eIconId)
	{
		return _arrIcons[ToIndex(eIconId)];
	}
}
