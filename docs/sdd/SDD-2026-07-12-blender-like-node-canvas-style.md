# Mini SDD: Blender-like Node Canvas Style

## Goal

Make the generic node canvas visually closer to Blender's material nodes: compact full-width colored headers, socket rows, type-colored pins, inline default-value badges, and collapsible-looking section rows.

## Non-goals

- No material graph asset schema.
- No shader compiler, preview, or runtime material changes.
- No direct Editor dependency on ImGui or imgui-node-editor.

## Files

- `project/src/engine/Function/Gui/UINodeGraph.h`
- `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasWidget.*`
- `project/src/editor/Panels/NodeCanvasDemoPanel.cpp`
- `docs/EditorNodeCanvasWidget.md`

## Approach

Extend `UINodeGraphModel` with generic UI metadata for row-based node bodies while keeping legacy pin-list rendering as fallback. Render rows through `UIContext` and the engine-side `UINodeEditor` facade only.

## Verification

- `git diff --check -- . ':(exclude)product/config/editor/imgui.ini'`
- Editor raw ImGui scan excluding the legacy `project/src/editor/ImGui` folder.
- `build_editor.bat Debug x64`
- `RunArchGate.bat`
- `run.bat editor Debug --smoke-test-seconds=5`

## Risk / rollback

Risk is visual/layout regression in generic node canvas. Roll back the added row metadata and renderer path; legacy pin-list rendering remains intact for nodes without `bodyRows`.
