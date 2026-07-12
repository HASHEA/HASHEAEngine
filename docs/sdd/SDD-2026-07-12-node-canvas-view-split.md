# Mini SDD: Node Canvas View Split

## Goal

Split the generic node canvas rendering into focused view/style helpers so node styling, port rendering, link rendering, and canvas interaction can evolve independently.

## Non-goals

- No material graph asset schema, compiler, preview, or runtime material behavior.
- No new third-party dependency.
- No Editor-side direct ImGui or `imgui-node-editor` usage.
- No broad visual redesign beyond preserving the current Blender-like style and making inline controls background-transparent through a shared style helper.

## Files

- `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasWidget.*`
- `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasStyle.*`
- `project/src/editor/Widgets/NodeGraph/NodeGraphNodeView.*`
- `project/src/editor/Widgets/NodeGraph/NodeGraphPortView.*`
- `project/src/editor/Widgets/NodeGraph/NodeGraphLinkView.*`
- `docs/EditorNodeCanvasWidget.md`

## Approach

Keep `NodeGraphCanvasWidget` as the canvas coordinator for graph submission, selection, create/delete interaction, context menus, and navigation. Move node layout/header/body drawing into `NodeGraphNodeView`, pin marker and pin row rendering into `NodeGraphPortView`, link submission into `NodeGraphLinkView`, and all shared metrics/colors/transparent-control styling into `NodeGraphCanvasStyle`.

## Verification

- `git diff --check -- . ':(exclude)product/config/editor/imgui.ini'`
- Focused raw ImGui/node-editor scan under `project/src/editor/Widgets/NodeGraph`
- `build_editor.bat Debug x64`

## Risk / rollback

Risk is accidental layout or interaction regression while moving code. Roll back the split files and restore the previous monolithic `NodeGraphCanvasWidget.cpp`; the data model and engine facade remain unchanged.
