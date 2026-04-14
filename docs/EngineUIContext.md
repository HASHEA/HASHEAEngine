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
  - text, buttons, checkboxes, combo boxes, tree nodes
  - style color/style var helpers
  - cursor/layout helpers
- Render target presentation:
  - `register_render_target`
  - `unregister_render_target`
  - `get_render_target_texture_id`
  - `image`
  - `draw_render_target`
  - `draw_render_target_fill_available`
- Input capture queries:
  - `wants_capture_mouse`
  - `wants_capture_keyboard`
  - `wants_text_input`

## Non-Responsibilities

The following do not belong in `UIContext`:

- editor workspace orchestration
- dockspace layout policy
- panel registration/lifetime conventions
- inspector/property-grid semantics
- editor-only viewport conventions

Those concerns should be handled by a dedicated editor/tool facade built on top of `UIContext`.

## Backend Model

- Runtime RHI selection chooses Vulkan or DX12.
- `ImGuiLayer` binds to the active backend internally.
- `UIContext` stays backend-agnostic.
- Vulkan backend setup in `ImGuiLayer` must match the engine render path.
- When the Vulkan RHI is using dynamic rendering, `ImGui_ImplVulkan_InitInfo::UseDynamicRendering` and the swapchain color attachment format must be filled explicitly.

## Render Target Path

- Higher layers pass `RenderTarget` objects to `UIContext`.
- `UIContext` forwards them to the internal backend layer.
- Backend descriptor/SRV registration is owned by the engine.
- `UITextureHandle` is opaque and should be treated as transient backend data.

## Current Integration

- `Application` owns one `UIContext`.
- Window text/key/mouse events are forwarded into `UIContext`.
- `Renderer::end_frame()` composes UI as the final overlay pass on the back buffer.

## Typical Usage Pattern

- Open one or more windows for debug or tool workflows.
- Compose tables, trees, menus, tabs, and images inside those windows.
- Use `draw_render_target_fill_available` for debug previews and tool viewports when a simple fit-to-region behavior is enough.

## Extension Rule

- Add a feature to `UIContext` only if it is backend-agnostic, immediate-mode, and reusable by more than just the editor workspace.
- If a requested feature encodes editor workflow or layout policy, implement it in a higher editor-specific facade instead of extending `UIContext` directly.
