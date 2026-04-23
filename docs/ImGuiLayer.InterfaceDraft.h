#pragma once

// Historical draft reference only.
// This header is not the current Engine / Editor interface contract.
// Prefer docs/Editor.UIContextGapChecklist.md and docs/EditorToEngineGapChecklist.md.

#include <memory>

struct ImVec2;
typedef void* ImTextureID;

namespace AshEngine
{
	class RenderTarget;

	struct ImGuiLayerConfig
	{
		bool enable_docking = true;
		bool enable_viewports = false;
		bool enable_keyboard_navigation = true;
		bool enable_gamepad_navigation = false;
		const char* ini_path = nullptr;
	};

	// Draft interface for engine-side implementation.
	// This file is intentionally kept out of the runtime build and serves as
	// a contract for the engine teammate to implement in Function layer.
	class ImGuiLayer
	{
	public:
		virtual ~ImGuiLayer() = default;

	public:
		// Initialize ImGui context and backend bindings.
		virtual bool init(const ImGuiLayerConfig& config = {}) = 0;

		// Begin a new ImGui frame. Returns false when the backend is not ready.
		virtual bool begin_frame() = 0;

		// Render current frame draw data to the active back buffer.
		virtual void render() = 0;

		// Shutdown backend objects and destroy ImGui context.
		virtual void shutdown() = 0;

		// Convert an engine RenderTarget into an ImGui-displayable texture id.
		// The implementation owns any backend-specific descriptor/SRV objects.
		virtual ImTextureID register_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;

		// Release backend-side registration for a RenderTarget.
		virtual void unregister_render_target(const std::shared_ptr<RenderTarget>& render_target) = 0;

		// Query current texture id for an already registered RenderTarget.
		// Useful when the target was internally recreated after resize.
		virtual ImTextureID get_render_target_texture_id(const std::shared_ptr<RenderTarget>& render_target) const = 0;

		// Convenience helper for editor viewport rendering.
		virtual void draw_render_target(const std::shared_ptr<RenderTarget>& render_target, const ImVec2& size) = 0;

		// Input capture state for editor input routing.
		virtual bool wants_capture_mouse() const = 0;
		virtual bool wants_capture_keyboard() const = 0;
		virtual bool wants_text_input() const = 0;
	};
}
