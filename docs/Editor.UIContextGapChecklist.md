# Editor UIContext Gap Checklist

## 1. 当前结论

这轮补完以后，**编辑器活跃运行路径已经不再直接依赖原生 `ImGui` API 或 `imgui.h`**。

当前仍保留原生 `ImGui` 的只有历史参考目录：

- `project/src/editor/ImGui/EditorImGuiLayer.cpp`
- `project/src/editor/ImGui/EditorStyle.cpp`

它们已经被 premake 排除，不参与运行时构建。

---

## 2. 这轮补到 UIContext 的能力

### 2.1 输入 / popup / 快捷键

- `begin_popup_context_item(...)`
- `is_any_item_hovered()`
- `is_any_item_active()`
- `is_mouse_released(...)`
- `is_mouse_double_clicked(...)`
- `is_window_focused_with_children()`
- `is_window_hovered_with_children()`
- `is_key_chord_pressed(...)`

### 2.2 key chord 抽象

已经补齐：

- `UIKey`
- `UIModifierFlags`
- `UIKeyChord`
- `make_key_chord(...)`

因此编辑器动作系统已经不需要再直接写：

- `ImGuiKey_*`
- `ImGuiMod_*`

### 2.3 drag-drop 抽象

已经补齐：

- `UIDragDropPayload`
- `begin_drag_drop_source(...)`
- `set_drag_drop_payload(...)`
- `end_drag_drop_source()`
- `begin_drag_drop_target()`
- `accept_drag_drop_payload(...)`
- `end_drag_drop_target()`
- `has_drag_drop_payload()`
- `is_drag_drop_payload_active(...)`

### 2.4 几何查询 / 定制绘制

- `set_next_item_open(...)`
- `get_item_rect()`
- `get_cursor_screen_pos()`
- `get_style_frame_padding()`
- `get_style_item_spacing()`
- `get_style_color(...)`
- `calc_text_size(...)`
- `get_mouse_pos()`
- `get_time_seconds()`
- `get_font_size()`
- `get_tree_node_to_label_spacing()`
- `draw_window_image(...)`
- `draw_window_rect_filled(...)`
- `draw_window_rect(...)`
- `draw_window_line(...)`
- `draw_window_text(...)`
- `push_window_clip_rect(...)`
- `pop_window_clip_rect()`

---

## 3. 已经完成收口的活跃路径

- `project/src/editor/Panels/AssetBrowserPanel.cpp`
- `project/src/editor/Panels/ViewportPanel.cpp`
- `project/src/editor/Panels/SceneHierarchyPanel.cpp`
- `project/src/editor/Panels/InspectorPanel.cpp`
- `project/src/editor/Panels/ConsolePanel.cpp`
- `project/src/editor/Widgets/EditorTreeWidget.cpp`
- `project/src/editor/App/EditorApplication.cpp`

这意味着：

- AssetBrowser 的局部快捷键已经统一走 `EditorShortcutService`
- SceneHierarchy 的拖拽 reparent 已经通过 `UIContext` drag-drop 接口承载
- EditorTreeWidget 的 tree row、auto expand、drag-drop、定制绘制已经不再直接触碰原生 `ImGui`
- 编辑器快捷键定义已经走 engine-owned key/chord 抽象

---

## 4. 当前剩余的 UIContext 关注点

现在不再是“必须补、不然编辑器会回退到原生 ImGui”的级别了，剩下的是增强项：

### P1：更高层的 tree/list primitive

当前 `EditorTreeWidget` 已经可以只靠 `UIContext` 拼出来，但它仍然是“组合式”的。

如果后面要继续做：

- 多列树
- 多选树
- 更复杂的树节点状态
- 统一的 row action 区

可以再考虑是否要给 `UIContext` 增加更高层的 tree/list primitive。

### P1：更显式的 draw list facade

这轮先补的是 editor-safe 的窗口绘制函数。

如果后面要继续做：

- gizmo overlay
- richer viewport HUD
- thumbnail badge
- overlay handles

可以再评估是否要抽一层更显式的 `UIDrawList` facade。

---

## 5. 维护规则

后续继续做编辑器功能时，遵循这三条：

1. 运行时编辑器路径优先使用 `UIContext`
2. 如果缺的是通用立即模式 UI 原语，先补 `UIContext`
3. 不要把历史参考目录里的原生 `ImGui` 实现重新带回活跃路径
