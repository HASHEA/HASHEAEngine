# Engine UIContext

`UIContext` is the engine-facing developer UI facade.

Its target is shared tooling and debug UI that may be used by editor code, game-side debug tools, and client-side developer utilities. It is not intended to be an editor workspace framework.

## Layering

- `ImGuiLayer` stays internal to the engine module and owns backend-specific ImGui integration.
- `UIContext` is the public engine facade exposed to higher layers.
- Higher layers should not include ImGui headers or touch Vulkan/DX12 ImGui backends directly.
- Editor-specific abstractions should live in a separate layer above `UIContext`.

## Responsibilities

- Engine-owned lifecycle:
  - `init`
  - `begin_frame`
  - `render`
  - `shutdown`
- Common developer UI primitives:
  - windows, child windows, menus, tabs, tables, popups
  - docking and viewport primitives
  - text, buttons, checkboxes, combo boxes, tree nodes
  - wrapped text
  - vec and color editing helpers
  - style color/style var helpers
  - cursor/layout helpers
- Render target and scene surface presentation:
  - `register_render_target`
  - `unregister_render_target`
  - `get_render_target_texture_id`
  - `image`
  - `image_surface`
  - `draw_render_target`
  - `draw_render_target_fill_available`
  - `draw_surface_fill_available`
- Input capture queries:
  - `wants_capture_mouse`
  - `wants_capture_keyboard`
  - `wants_text_input`

## Non-Responsibilities

The following do not belong in `UIContext`:

- editor workspace orchestration
- default dockspace layout policy and panel placement policy
- panel registration/lifetime conventions
- inspector/property-grid semantics
- editor-only viewport conventions

Those concerns should be handled by a dedicated editor/tool facade built on top of `UIContext`.

## Generic Docking Boundary

`UIContext` may expose raw, backend-agnostic docking and viewport primitives when they are still immediate-mode building blocks instead of editor policy.

Good examples:

- `begin_dockspace_host_window`
- `dock_space`
- `dock_builder_*`
- `get_main_viewport_rect`
- `get_main_viewport_id`
- `set_next_window_viewport`

These helpers are intentionally low level. They let higher layers stay off raw ImGui without forcing the Engine to own editor layout conventions.

What still stays out:

- the editor's default dock graph
- named panel placement rules
- workspace reset policy
- panel persistence conventions

## Backend Model

- Runtime RHI selection chooses Vulkan or DX12.
- `ImGuiLayer` binds to the active backend internally.
- `UIContext` stays backend-agnostic.
- Vulkan backend setup in `ImGuiLayer` must match the engine render path.
- When the Vulkan RHI is using dynamic rendering, `ImGui_ImplVulkan_InitInfo::UseDynamicRendering` and the swapchain color attachment format must be filled explicitly.

## Render Target And Scene Surface Paths

- Higher layers may pass `RenderTarget` objects to `UIContext` when they already own a generic/custom render target.
- For scene-driven viewports, higher layers should pass a `UISurfaceHandle` obtained from `ScenePresentationSubsystem`.
- `UIContext` resolves a `UISurfaceHandle` back to the current engine-owned offscreen `RenderTarget` internally.
- `Window` outputs do not expose a valid `UISurfaceHandle`, because swapchain/back-buffer resources are not UI-sampled surfaces.
- Backend descriptor/SRV registration is owned by the engine.
- `UITextureHandle` is opaque and should be treated as transient backend data.

## Current Integration

- `Application` owns one `UIContext`.
- `Application` also owns one `ScenePresentationSubsystem`.
- Window text/key/mouse events are forwarded into `UIContext`.
- `UIContext` resolves scene surfaces through `Application::get_scene_presentation()`.
- Engine-owned overlays may host shared debug controls through `UIContext`; the Render Debug View window is one example and only selects an engine render pass that writes the chosen RT visualization back to the main output.
- `Renderer::end_frame()` composes UI as the final overlay pass on the back buffer.

## Typical Usage Pattern

- Open one or more windows for debug or tool workflows.
- Compose tables, trees, menus, tabs, and images inside those windows.
- Use the docking/viewport helpers when a tool wants full-window dock hosting without dropping to raw ImGui.
- Use `draw_render_target_fill_available` for generic/custom previews when a simple fit-to-region behavior is enough.
- Use `draw_surface_fill_available` for scene-driven viewports backed by `ScenePresentationSubsystem`.

## Extension Rule

- Add a feature to `UIContext` only if it is backend-agnostic, immediate-mode, and reusable by more than just the editor workspace.
- If a requested feature encodes editor workflow or layout policy, implement it in a higher editor-specific facade instead of extending `UIContext` directly.
