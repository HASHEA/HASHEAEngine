# SDD-2026-07-12: Engine-Side Node Editor Facade

## Status
Implemented; generic graph model/widget and visual/content layer added; final smoke rerun pending

## Context
The material editor needs a production-ready node canvas. Dear ImGui is compiled and hosted inside `Engine.dll`, while editor code must only draw through engine UI facades. The selected approach is to compile `thedmd/imgui-node-editor` into `Engine.dll` and expose a narrow AshEngine facade to editor panels.

## Goals
- Keep `imgui-node-editor` in the same module as Dear ImGui.
- Preserve the invariant that `project/src/editor` does not include or call raw ImGui / `ax::NodeEditor`.
- Provide a reusable engine-side node editor facade for editor node canvas work.
- Provide a reusable editor-side graph canvas widget above the facade, so future material editor views do not duplicate low-level node canvas interaction code.
- Prove the path with a small demo panel that combines a graph canvas and Details properties.

## Non-goals
- Implement the full material graph asset format.
- Implement graph serialization, node registry, compiler, or preview service.
- Mirror every `ax::NodeEditor` API in the facade.
- Change runtime material consumption (`Material`, `MaterialInstance`, `MaterialSystem`).

## Current implementation
- Entry points:
  - `project/src/engine/Function/Gui/UIContext.*` owns the UI boundary.
  - `project/src/editor/Panels/NodeCanvasDemoPanel.*` is a closed-by-default demo panel.
- Modules:
  - `Engine.dll` owns Dear ImGui and the node editor library.
  - `Editor.exe` uses only `AshEngine::UINodeEditor` and `UIContext`.
- Data flow:
  - Editor submits nodes, pins, and links each frame.
  - Engine facade binds the owned `ax::NodeEditor::EditorContext`, then forwards calls to the third-party library.
  - Node and link deletion use the library query/accept flow first, then update editor-owned graph draft data.
- Known constraints:
  - Editor active paths must not include `imgui.h` or `imgui_node_editor.h`.
  - Node graph authoring data must stay separate from future compiler/runtime material data.

## Proposal

### Module changes
| Module | Change | Files |
| --- | --- | --- |
| Engine build | Compile core `imgui-node-editor` sources into `Engine.dll` | `project/src/engine/premake5.lua` |
| Engine UI | Add `AshEngine::UINodeEditor` facade | `project/src/engine/Function/Gui/UINodeEditor.*` |
| Engine UI data | Add a pure data graph model for common node/pin/link rules | `project/src/engine/Function/Gui/UINodeGraph.h` |
| Editor UI | Add reusable graph canvas widget and demo panel | `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasWidget.*`, `project/src/editor/Panels/NodeCanvasDemoPanel.*` |
| Tests | Cover pure graph link and cleanup rules | `project/src/tests/Function/uinodegraph_tests.cpp` |
| Docs | Record node canvas boundary and demo location | `docs/EditorNodeCanvasWidget.md`, `docs/sdd/SDD-2026-07-12-node-editor-engine-facade.md` |

### API / contract changes
`AshEngine::UINodeEditor` is a new engine UI facade. It exposes node canvas primitives (`begin`, `begin_node`, `begin_pin`, `link`), link creation/deletion queries, selection queries, context menu support, coordinate conversion, and node position access. It intentionally hides all third-party and ImGui types.

`AshEngine::UINodeGraphModel` is a small pure data graph model for reusable editor canvas work. It validates output-to-input links, replaces an existing single input link, removes links, removes nodes, and cleans dangling links.

`AshEditor::NodeGraphCanvasWidget` owns a facade instance and turns a `UINodeGraphModel` into a reusable canvas control. It reports user intent through `NodeGraphCanvasResult` instead of owning domain-specific actions, undo/redo, serialization, or material semantics.

### Backend impact
No Vulkan/DX12 rendering behavior changes. The feature uses existing Dear ImGui rendering through `UIContext` / `ImGuiLayer`.

### Performance
Expected cost is per-frame immediate-mode node canvas submission. No PerfGate baseline changes are expected.

## Verification plan
| Verification | Coverage | Command |
| --- | --- | --- |
| Regenerate project files | Premake source list and filters | `generate_vs2022.bat` |
| Graph model unit tests | Link validation, single-link input replacement, node deletion cleanup | `RunTests.bat Debug --test-case=UINodeGraphModel*` |
| Build editor | Engine facade, third-party library compile/link, editor panel link | `build_editor.bat Debug x64` |
| Architecture scan | Editor raw ImGui/node-editor boundary | `RunArchGate.bat` plus focused `rg` scan |
| Manual UI smoke | Panel opens, node drag/link/delete/details path works | `run.bat editor` |

## Task breakdown
1. Vendor/compile `imgui-node-editor` into Engine.
2. Add `UINodeEditor` facade.
3. Add demo panel and keep it closed by default.
4. Move demo from hard-coded nodes to graph draft data.
5. Add reusable `UINodeGraphModel` and `NodeGraphCanvasWidget`.
6. Add safe graph-item deletion, toolbar actions, pin marker anchoring, and shortcut-scope isolation.
7. Update docs and build/test/smoke verification.

## Follow-up implementation plan: generic canvas visual/content layer

Goal: make the reusable graph canvas expressive enough for material-editor style nodes without introducing material-specific graph semantics.

1. Extend `UINodeGraphNode` and `UINodeGraphPin` with pure UI metadata: category/type labels, subtitle, accent color, pin type label, pin shape, and body text rows.
2. Extend `NodeGraphCanvasOptions` with optional callbacks for drawing node body content and handling richer context-menu intents.
3. Replace the flat node rendering with a structured default layout: colored header, muted category/type text, body section, left input rows, right output rows, fixed pin markers, and stable minimum width.
4. Expand the background context menu to publish intents for add node, delete selected, reset view, break links, copy/paste placeholders, and add-comment placeholder.
5. Update `NodeCanvasDemoPanel` to populate metadata and draw a small inline editable body while keeping the Details panel path intact.
6. Add pure model tests for UI metadata defaults only where logic is affected; rely on editor build/smoke for immediate-mode visual behavior.
7. Update docs to clarify that `UINodeGraphModel` is generic UI graph data, while material-specific node definitions and commands remain future work.

## Risks
| Risk | Mitigation |
| --- | --- |
| Facade grows into a complete third-party mirror | Expose only APIs used by material editor milestones |
| Editor accidentally depends on raw ImGui | Keep includes constrained to Engine; verify with `rg` |
| Node-editor settings persistence conflicts with material graph layout | Disable third-party settings file; graph/document layer owns persisted layout |
| Future material graph commands need undo/redo | Keep demo graph draft separate; formal command layer lands with MaterialGraphDocumentService |

## Open questions
- `UINodeEditor` remains a separate engine UI facade instead of a `UIContext` member. This keeps each canvas owning an explicit node-editor context while preserving the same UI boundary rule.
- How much of selection/navigation state should be mirrored into formal material graph editor state.

## Outcome
- `imgui-node-editor` core sources are compiled into `Engine.dll`.
- `AshEngine::UINodeEditor` owns one node-editor context per instance and exposes only engine types.
- `NodeCanvasDemoPanel` proves data-driven nodes, pins, links, link creation/deletion, selection, node position sync, and Details integration.
- `UINodeGraphModel` centralizes reusable node/pin/link rules and is covered by doctest cases.
- `NodeGraphCanvasWidget` provides the generic editor canvas layer that the real material graph view can reuse.
- `UINodeGraphModel` now carries generic visual metadata for node category/type/subtitle/body rows, accent colors, pin type labels, pin colors, and pin shapes.
- `NodeGraphCanvasWidget` now supports richer default node styling, opt-in inline node body callbacks, and right-click menu actions without owning material-specific semantics.
- The demo supports adding nodes, resetting layout, zooming to content, deleting selected nodes/links, and cleaning connected links when a node is removed.
- The demo uses inline node editing for lightweight fields while keeping the Details panel as the full property editor path.
- Output pins are right-aligned, pin anchors use fixed visible markers, and the demo node body has a minimum width.
- Link deletion no longer mixes manual node-editor deletion calls with the library `BeginDelete` queue, avoiding the previously observed crash in `DeleteItemsAction::QueryItem`.
- Long-lived docs record `UINodeEditor` as an Engine-side UI facade.
