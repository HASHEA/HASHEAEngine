# Editor UI Facade Proposal

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

- workspace and dockspace orchestration
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
- text and common widgets
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

Docking support can still be enabled internally in ImGui for future editor usage. That does not require exposing dockspace/workspace policy directly on `UIContext`.

If the editor later needs dockspace helpers without touching ImGui directly, those helpers should be surfaced by an editor-specific facade built on top of `UIContext`, not by turning `UIContext` itself into an editor framework.

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
