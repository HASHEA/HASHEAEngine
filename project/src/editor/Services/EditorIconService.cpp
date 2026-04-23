#include "Services/EditorIconService.h"

#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RHICommon.h"
#include "Graphics/Texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace AshEditor
{
	namespace
	{
		constexpr const char* k_editorIconRoot = "product/assets/editor-temp/unreal-icons";

		struct EditorIconDefinition
		{
			EditorIconId id = EditorIconId::FolderClosed;
			const char* relative_path = nullptr;
			const char* debug_name = nullptr;
		};

		constexpr std::array<EditorIconDefinition, static_cast<size_t>(EditorIconId::Count)> k_iconDefinitions{ {
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

		auto to_index(EditorIconId icon_id) -> size_t
		{
			return static_cast<size_t>(icon_id);
		}
	}

	bool EditorIconService::initialize(const std::filesystem::path& workspace_root)
	{
		shutdown();
		m_workspaceRoot = workspace_root;
		m_iconRoot = workspace_root / k_editorIconRoot;
		register_default_icons();
		return true;
	}

	void EditorIconService::shutdown(AshEngine::UIContext* ui_context)
	{
		AshEngine::UIContext* active_ui_context = ui_context ? ui_context : m_registeredUiContext;
		if (active_ui_context)
		{
			for (IconEntry& icon : m_icons)
			{
				if (icon.handle && icon.texture_view)
				{
					active_ui_context->unregister_texture_view(icon.texture_view);
				}
			}
		}

		for (IconEntry& icon : m_icons)
		{
			icon = {};
		}

		m_registeredUiContext = nullptr;
		m_iconRoot.clear();
		m_workspaceRoot.clear();
	}

	AshEngine::UITextureHandle EditorIconService::get_icon(EditorIconId icon_id, AshEngine::UIContext& ui_context)
	{
		if (m_registeredUiContext != &ui_context)
		{
			clear_handles();
			m_registeredUiContext = &ui_context;
		}

		IconEntry& icon = get_entry(icon_id);
		if (!ensure_icon_loaded(icon))
		{
			return nullptr;
		}

		if (!icon.handle)
		{
			icon.handle = ui_context.register_texture_view(icon.texture_view);
			if (!icon.handle)
			{
				HLogWarning("EditorIconService failed to register icon '{}'.", icon.file_path.generic_string());
			}
		}

		return icon.handle;
	}

	void EditorIconService::register_default_icons()
	{
		for (const EditorIconDefinition& definition : k_iconDefinitions)
		{
			IconEntry& icon = get_entry(definition.id);
			icon.file_path = m_iconRoot / definition.relative_path;
			icon.debug_name = definition.debug_name ? definition.debug_name : "";
		}
	}

	void EditorIconService::clear_handles()
	{
		for (IconEntry& icon : m_icons)
		{
			icon.handle = nullptr;
		}
	}

	bool EditorIconService::ensure_icon_loaded(IconEntry& entry)
	{
		if (entry.texture_view)
		{
			return true;
		}
		if (entry.load_failed)
		{
			return false;
		}
		if (entry.file_path.empty())
		{
			entry.load_failed = true;
			return false;
		}
		if (!std::filesystem::exists(entry.file_path))
		{
			HLogWarning("EditorIconService could not find icon '{}'.", entry.file_path.generic_string());
			entry.load_failed = true;
			return false;
		}

		RHI::GraphicsContext* graphics_context = AshEngine::Application::get_graphics_context();
		if (!graphics_context)
		{
			HLogWarning("EditorIconService skipped loading '{}' because GraphicsContext is unavailable.", entry.file_path.generic_string());
			return false;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load(entry.file_path.string().c_str(), &width, &height, &channels, 4);
		if (!pixels)
		{
			HLogWarning(
				"EditorIconService failed to decode '{}': {}.",
				entry.file_path.generic_string(),
				stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			entry.load_failed = true;
			return false;
		}

		bool loaded = false;
		do
		{
			if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
			{
				HLogWarning("EditorIconService rejected '{}' because the image size is invalid ({}x{}).", entry.file_path.generic_string(), width, height);
				break;
			}

			RHI::TextureCreation texture_creation{};
			texture_creation.width = static_cast<uint16_t>(width);
			texture_creation.height = static_cast<uint16_t>(height);
			texture_creation.depth = 1;
			texture_creation.array_layer_count = 1;
			texture_creation.mip_level_count = 1;
			texture_creation.format = RHI::ASH_FORMAT_R8G8B8A8_SRGB;
			texture_creation.type = RHI::Ash_Texture2D;
			texture_creation.initial_state = RHI::AshResourceState::SRVGraphics;
			texture_creation.memoryType = RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
			texture_creation.uUsageFlags = RHI::ASH_TEXTURE_USAGE_SAMPLED_BIT;
			texture_creation.initial_data = pixels;
			texture_creation.name = entry.debug_name.c_str();

			entry.texture = graphics_context->create_texture(texture_creation);
			if (!entry.texture)
			{
				HLogWarning("EditorIconService failed to create GPU texture for '{}'.", entry.file_path.generic_string());
				break;
			}

			entry.texture_view = entry.texture->get_default_srv();
			if (!entry.texture_view)
			{
				HLogWarning("EditorIconService failed to create SRV for '{}'.", entry.file_path.generic_string());
				entry.texture.reset();
				break;
			}

			loaded = true;
		} while (false);

		stbi_image_free(pixels);

		entry.load_failed = !loaded;
		return loaded;
	}

	EditorIconService::IconEntry& EditorIconService::get_entry(EditorIconId icon_id)
	{
		return m_icons[to_index(icon_id)];
	}
}
