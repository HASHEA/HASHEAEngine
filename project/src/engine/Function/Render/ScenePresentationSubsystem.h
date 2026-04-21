#pragma once

#include "Base/hcore.h"
#include "Function/Render/ScenePresentationHandles.h"
#include "Function/Scene/Scene.h"
#include <glm/glm.hpp>
#include <memory>

namespace AshEngine
{
	class RenderAssetManager;
	class RenderTarget;
	class Renderer;
	class SceneRenderer;
	class UIContext;

	enum class SceneOutputKind : uint8_t
	{
		Window = 0,
		Offscreen
	};

	enum class SceneOutputFormat : uint8_t
	{
		Auto = 0,
		SRGB8,
		RGBA16F
	};

	struct ASH_API SceneOutputDesc
	{
		const char* debug_name = nullptr;
		SceneOutputKind kind = SceneOutputKind::Offscreen;
		uint32_t width = 1;
		uint32_t height = 1;
		SceneOutputFormat format = SceneOutputFormat::Auto;
		bool srgb = true;
	};

	enum class SceneCameraSource : uint8_t
	{
		PrimaryCamera = 0,
		EntityId
	};

	struct ASH_API SceneCameraSelector
	{
		SceneCameraSource source = SceneCameraSource::PrimaryCamera;
		EntityId entity_id = 0;
	};

	enum class SceneClearMode : uint8_t
	{
		Default = 0,
		Clear,
		Preserve,
		DontCare
	};

	enum class SceneViewRectMode : uint8_t
	{
		FullOutput = 0,
		PixelRect
	};

	struct ASH_API ScenePixelRect
	{
		int32_t x = 0;
		int32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	struct ASH_API SceneViewShowFlags
	{
		uint64_t bits = 0;

		static SceneViewShowFlags default_flags()
		{
			return {};
		}
	};

	struct ASH_API SceneViewOverrides
	{
		SceneClearMode color_clear_mode = SceneClearMode::Default;
		glm::vec4 clear_color{ 0.025f, 0.03f, 0.05f, 1.0f };

		SceneClearMode depth_clear_mode = SceneClearMode::Default;
		float clear_depth = 1.0f;

		SceneViewRectMode rect_mode = SceneViewRectMode::FullOutput;
		ScenePixelRect rect{};

		SceneViewShowFlags show_flags = SceneViewShowFlags::default_flags();
	};

	struct ASH_API SceneViewBindingDesc
	{
		const char* debug_name = nullptr;
		Scene* scene = nullptr;
		SceneCameraSelector camera{};
		SceneOutputHandle output{};
		SceneViewOverrides overrides{};
		bool enabled = true;
		int32_t sort_order = 0;
	};

	class ASH_API ScenePresentationSubsystem
	{
	public:
		ScenePresentationSubsystem();
		~ScenePresentationSubsystem();

		ScenePresentationSubsystem(const ScenePresentationSubsystem&) = delete;
		ScenePresentationSubsystem& operator=(const ScenePresentationSubsystem&) = delete;

	public:
		bool initialize(Renderer* renderer, RenderAssetManager* render_asset_manager, SceneRenderer* scene_renderer);
		void shutdown();

		SceneOutputHandle create_output(const SceneOutputDesc& desc);
		bool update_output(SceneOutputHandle handle, const SceneOutputDesc& desc);
		void destroy_output(SceneOutputHandle handle);

		SceneViewBindingHandle create_view_binding(const SceneViewBindingDesc& desc);
		bool update_view_binding(SceneViewBindingHandle handle, const SceneViewBindingDesc& desc);
		void destroy_view_binding(SceneViewBindingHandle handle);

		bool set_binding_enabled(SceneViewBindingHandle handle, bool enabled);
		bool request_refresh(SceneViewBindingHandle handle);

		UISurfaceHandle get_ui_surface(SceneOutputHandle handle) const;

		bool update_presentations();
		bool submit_presentations();

	private:
		std::shared_ptr<RenderTarget> resolve_surface_render_target(UISurfaceHandle surface) const;

	private:
		friend class UIContext;

	private:
		class Impl;
		std::unique_ptr<Impl> m_impl{};
	};
}
