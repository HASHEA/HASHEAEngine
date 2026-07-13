# Editor Node Canvas

The editor node canvas uses `thedmd/imgui-node-editor`, compiled into `Engine.dll` beside Dear ImGui. Editor code must not include `imgui_node_editor.h` or call `ax::NodeEditor` directly.

## Boundary

- `project/thirdparty/imgui-node-editor/` contains the vendored third-party library.
- `project/src/engine/premake5.lua` compiles only the core node-editor sources into `Engine`.
- `project/src/engine/Function/Gui/UINodeEditor.h` exposes the engine-side facade.
- `project/src/engine/Function/Gui/UINodeGraph.h` contains the pure data graph model used by generic editor canvases.
- Editor panels use reusable widgets and `UIContext`; they must not include raw ImGui or `imgui_node_editor.h`.

## Generic widget

- `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasWidget.*` is the reusable canvas control.
- `project/src/editor/Widgets/NodeGraph/NodeGraphCanvasStyle.*` centralizes shared metrics, colors, pin/value color resolution, and transparent inline-control styling.
- `project/src/editor/Widgets/NodeGraph/NodeGraphNodeView.*` draws one node: header, collapse hit area, text body, body rows, sections, and row value slots.
- `project/src/editor/Widgets/NodeGraph/NodeGraphPortView.*` draws fixed pin markers, input/output pin rows, and legacy paired pin rows.
- `project/src/editor/Widgets/NodeGraph/NodeGraphLinkView.*` submits graph links to the engine-side node-editor facade.
- `project/src/editor/Widgets/NodeGraph/NodeGraphNodeTypeRegistry.*` loads JSON node UI templates and instantiates runtime nodes with generated node/pin ids.
- The widget owns one `AshEngine::UINodeEditor` instance and draws an `AshEngine::UINodeGraphModel` each frame.
- The model owns graph truth and generic UI metadata: nodes, pins, links, category/type labels, subtitle/body lines, accent colors, pin labels, pin value kinds, pin types, pin colors, pin shapes, row-based node bodies, inline default-value displays, editable-value intent flags, inline editor kinds, collapsible flags, and collapsible-looking sections.
- The model also owns reusable link rules: pin direction checks, input single-link replacement, link removal, node removal cleanup, connected-link break, collapsible node body state, and dangling-link cleanup.
- The widget reports intent through `NodeGraphCanvasResult`: create-node request, create-link request, body-edited state, context/menu actions, focused state, selected node ids, and selected link ids.
- The caller remains responsible for domain data such as node properties, undo/redo commands, serialization, material semantics, and generated ids.
- `NodeGraphCanvasOptions::pDrawCreateMenu` lets callers replace the default single "Add Node" item with a domain-specific create palette. When it returns a selected type id, `NodeGraphCanvasResult::strCreateNodeTypeId` carries that id back to the caller.
- Internal canvas popup ids are derived from `NodeGraphCanvasOptions::pCanvasId`, so multiple node canvases can be drawn in the same frame without sharing popup state.
- `NodeGraphCanvasOptions::pDrawNodeBody` lets callers embed domain-specific `UIContext` controls inside a node body without the canvas knowing the domain. Inline body drawing is disabled by default and must be explicitly enabled by the caller.
- `NodeGraphCanvasOptions::pDrawBodyRowValue` enables compact, domain-specific row editors only when `UINodeGraphBodyRow::bEditableDefaultValue` is set. The default Blender-like presentation stays display-only: color values render as swatches, numeric values render as compact badges, and full editing is expected to happen in Details unless a caller supplies a purpose-built mini control.

## Node type JSON

- `product/config/editor/node_types/demo_node_types.json` is the demo registry for configurable node UI templates.
- JSON node types describe UI defaults only: `id`, `title`, `category`, `typeLabel`, `subtitle`, `accentColor`, `collapsible`, `defaultCollapsed`, `inputs`, `outputs`, `rows`, and `sections`.
- Pin template ids are strings in JSON; `NodeGraphNodeTypeRegistry::CreateNode` takes a `NodeGraphNodeCreateDesc`, returns a `NodeGraphNodeCreateResult`, assigns runtime numeric `UIPinId` values, maps row `input` / `output` references to those ids, and exposes template-to-runtime pin mappings through `FindPinId`.
- Each node type requires unique pin template ids across inputs and outputs. Row `input` references must point to an input pin template id, and row `output` references must point to an output pin template id.
- Row templates can declare `valueKey`, `valueKind`, `editor`, `defaultText`, `defaultColor`, `editable`, and `showDefault`. The generic canvas only stores these fields; the caller decides how `valueKey` maps to domain values.
- The JSON schema is not the future material graph asset format. It is a reusable editor UI template layer that material nodes can adapt from later.

## Demo

- `project/src/editor/Panels/NodeCanvasDemoPanel.cpp` shows a minimal graph draft using `UINodeGraphModel` and `NodeGraphCanvasWidget`, with richer node styling, JSON/registry-backed right-click node creation, opt-in inline node body editing, link creation/deletion, node selection, node layout sync, right-click actions, and Details integration through `PropertyEditorWidget`.
- Nodes with `UINodeGraphNode::bodyRows` render in a Blender-like material-node style: visually edge-to-edge colored headers with rounded top corners and square bottom corners, compact left/right socket rows, right-aligned output-only labels, type-colored pins, compact color swatches or numeric badges, and section rows. Nodes without `bodyRows` keep the legacy paired pin-row fallback.
- The demo panel is registered closed by default as `Node Canvas Demo`.

## Interaction model

- Links are created through the library `BeginCreate` / `QueryNewLink` / `AcceptNewItem` flow.
- Links and nodes are deleted through the library `BeginDelete` / `QueryDeleted*` / `AcceptDeletedItem` flow; the editor only mutates its own graph data after the library accepts the deletion.
- Node deletion removes the node and all connected links from the editor graph draft so no orphan links remain.
- Toolbar actions cover adding a demo node, resetting layout, zooming to content, and deleting the selected graph item.
- Pin markers are drawn by the engine facade and register `PinRect` / `PinPivotRect`, so link anchors stay attached to the visible fixed marker shape.
- Output pins are right-aligned inside the node; input pins stay on the left.
- Pin colors can be supplied explicitly or derived from `UINodeGraphValueKind` for common material-editor data types such as scalar, vector, color, texture, shader, bool, and string.
- Background context menu publishes actions for add node, delete selected, break links, reset view, copy/paste placeholders, and comment placeholders.
- Node and link context menus are routed through the engine-side facade and publish node/link-specific actions such as collapse/expand, break node links, delete node, and delete link.
- The header chevron is an actual clickable hit area, not just decoration, and toggles `UINodeGraphNode::bCollapsed`.
- The node canvas publishes `NodeCanvasContent` shortcut scope while focused, preventing global scene `Delete` from deleting entities when graph items are selected.

## Material editor path

- The material editor should reuse `NodeGraphCanvasWidget` for canvas interaction, then layer material-specific document state, node definitions, commands, palette, compiler diagnostics, and preview services on top.
- `UINodeGraphModel` is intentionally generic UI graph data, not the future persisted `AshMatGraph` asset schema.
