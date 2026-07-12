# Mini SDD: Node Type JSON Templates

## Goal

Make generic node UI types configurable from JSON so title color, type/category labels, ports, row controls, default values, and collapse defaults can be changed without editing the node canvas renderer.

## Non-goals

- No material graph asset schema.
- No material compiler, HLSL generation, preview, or runtime material binding.
- No user graph serialization format.
- No Editor-side direct ImGui or `imgui-node-editor` usage.

## Files

- `project/src/engine/Function/Gui/UINodeGraph.h`
- `project/src/editor/Widgets/NodeGraph/NodeGraphNodeTypeRegistry.*`
- `project/src/editor/Widgets/NodeGraph/NodeGraphNodeView.*`
- `project/src/editor/Panels/NodeCanvasDemoPanel.*`
- `product/config/editor/node_types/demo_node_types.json`
- `docs/EditorNodeCanvasWidget.md`

## Approach

Add generic UI metadata to `UINodeGraphNode` and `UINodeGraphBodyRow` for collapsibility and inline editor kind. Add an editor-side `NodeGraphNodeTypeRegistry` that loads JSON node type templates and instantiates `UINodeGraphNode` objects by assigning runtime node/pin ids. The demo panel uses the registry as its source for built-in demo nodes, while domain values still live in demo-only `NodeParams`.

## Verification

- `git diff --check -- . ':(exclude)product/config/editor/imgui.ini'`
- Focused raw ImGui/node-editor scan under `project/src/editor/Widgets/NodeGraph`
- `generate_vs2022.bat`
- `build_editor.bat Debug x64`
- `RunArchGate.bat`

## Risk / rollback

Risk is broken demo node creation if the JSON is invalid. The registry treats invalid entries as load failure and the demo falls back to built-in templates, so the canvas remains usable.
