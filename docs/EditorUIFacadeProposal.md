# Editor UI Facade Proposal

> 文档状态：**历史参考**
>
> 这份文档属于早期 UI 分层提案，保留用于回溯设计背景。
>
> 当前主线实现与协作口径请优先参考：
>
> - `docs/EditorDeveloperGuide.md`
> - `docs/Editor.UIContextGapChecklist.md`
> - `docs/EditorProgress.UIContextAcceptance.md`

This note summarizes the recommended UI layering after narrowing `UIContext` back to Engine-safe responsibilities.

## Goal

Keep the Engine public UI surface reusable by more than just the editor.

The same Engine module may later serve:

- editor tooling
- game-side debug UI
- client-side developer tools

Because of that, the Engine-level facade should expose generic developer UI primitives, not editor workspace semantics.

## Recommended Layering

### 1. `ImGuiLayer` (Engine internal only)

Responsibilities:

- own the ImGui context
- bind to Vulkan or DX12 backend
- forward platform input
- register Engine render targets as ImGui-displayable textures
- render draw data to the active back buffer

This layer must stay hidden from editor and gameplay code.

### 2. `UIContext` (Engine public developer UI facade)

Responsibilities:

- frame lifecycle
- input capture queries
- generic window/layout/widget primitives
- generic image and render-target presentation
- backend-agnostic DevUI entry point for tools/debugging

This is the layer that editor, game debug code, and client debug code may all share.

### 3. `EditorUIContext` or `EditorWorkspaceUI` (editor-facing facade)

Responsibilities:

- workspace host windows
- dockspace policy
- panel creation/open-state conventions
- inspector/property-grid helpers
- viewport panel conventions
- editor layout persistence rules
- editor-specific styling and interaction conventions

This layer is editor-specific even if it is implemented inside the Engine repository.

### 4. Editor application code

Responsibilities:

- actual editor features
- asset browser, outliner, inspector, log panels, viewport tabs
- editor commands, shortcuts, and feature-specific state

## What Should Stay Out of `UIContext`

These APIs are useful for an editor, but they encode editor semantics and should not be treated as Engine-wide UI primitives:

- workspace orchestration and default dock graph policy
- named panel abstractions
- property-grid row helpers
- inspector-specific label/value conventions
- viewport panel padding/fit policy conventions

If these concepts remain in `UIContext`, the Engine public API becomes biased toward one consumer: the editor.

## What Belongs in `UIContext`

Good `UIContext` additions usually satisfy all of these:

- backend-agnostic
- immediate-mode
- reusable outside the editor
- not coupled to a specific tool workflow

Examples:

- windows and child windows
- menus, tabs, tables, popups
- raw docking and viewport primitives
- text and common widgets
- wrapped text, vec editors, color editors
- tree controls
- image and render-target display helpers
- generic layout/style helpers

## Decision Rule For Future Additions

When a new UI helper is requested, ask:

1. Would game-side or client-side developer tooling plausibly use this as-is?
2. Does it describe a generic immediate-mode primitive rather than editor workflow?
3. Can it remain independent from editor object models and editor state conventions?

If the answer is "no" to any of those, the feature should likely live above `UIContext`.

## About Docking

Raw docking helpers are acceptable on `UIContext` as long as they stay at the primitive layer:

- create a dock host window
- create a dock space
- split nodes
- dock windows by name
- finish a builder graph

What should still stay above `UIContext`:

- the editor's default layout recipe
- panel naming/registration rules
- layout reset policy
- workspace persistence rules

## Relation To Runtime Game UI

`UIContext` should be treated as DevUI, not as the long-term runtime UI solution for shipped game/client interfaces.

Possible `UIContext` consumers:

- debug HUDs
- render target inspectors
- render graph viewers
- GPU/asset/debug panels

Non-goal:

- end-user runtime UI framework

That runtime-facing UI should remain a separate design path.
