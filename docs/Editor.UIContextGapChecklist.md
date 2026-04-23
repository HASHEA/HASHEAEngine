# Editor UIContext Gap Checklist

## 1. 审计目标

本清单用于回答 4 个问题：

1. 当前活跃运行路径里，Editor 是否已经继续只通过 `UIContext`
2. 仓库中还剩哪些原生 `ImGui` 依赖位置
3. 这些位置分别属于：
   - 活跃运行路径
   - 历史参考路径
   - 文档 / 草案参考
4. 后续如果要继续保持“运行时路径不回退到原生 ImGui”，`UIContext` 最优先该补哪些通用能力

---

## 2. 审计范围与方法

### 2.1 本轮审计范围

- `project/src/editor/App/**`
- `project/src/editor/Panels/**`
- `project/src/editor/Services/**`
- `project/src/editor/Core/**`
- `project/src/editor/Editor.h`
- `project/src/editor/Editor.cpp`
- `project/src/editor/ImGui/**`
- `project/src/editor/premake5.lua`

### 2.2 本轮检索口径

本轮只做只读审计，没有修改源码。

检索重点：

1. 原生 ImGui 依赖：
   - `ImGui::`
   - `imgui.h`
   - `imgui_internal.h`
2. `UIContext` 使用：
   - `UIContext`
   - `get_ui_context`
   - `begin_dockspace_host_window`
   - `dock_space`
   - `dock_builder_*`
   - `draw_render_target_fill_available`
   - 常用控件包装，如：
     - `input_text`
     - `drag_float3`
     - `color_edit3`
     - `tree_node`
     - `begin_table`
     - `menu_item`
     - `combo`
     - `checkbox`
     - `button`

说明：

- 由于当前沙箱里 `rg` 启动被拒，本轮使用 PowerShell 的 `Select-String` 做代码检索。
- 本轮结论仍可复现，不影响审计有效性。

---

## 3. 分类结果

### 3.1 A 类：活跃运行路径，当前审计结果为“已收口到 UIContext”

审计路径：

- `project/src/editor/App/**`
- `project/src/editor/Panels/**`
- `project/src/editor/Services/**`
- `project/src/editor/Core/**`
- `project/src/editor/Editor.*`

结果：

- 在上述活跃运行路径里，本轮没有检出 `ImGui::`、`imgui.h`、`imgui_internal.h`
- 活跃运行路径里已经能看到明确的 `UIContext` 入口与使用链

主要证据：

- `project/src/editor/App/EditorApplication.cpp`
  - `AshEngine::Application::get_ui_context()`
  - `begin_dockspace_host_window(...)`
  - `dock_space(...)`
  - `dock_builder_*`
  - `begin_main_menu_bar()`
  - `menu_item(...)`
- `project/src/editor/Core/EditorPanel.cpp`
  - `begin_window(...)`
  - `end_window(...)`
- `project/src/editor/Panels/ViewportPanel.cpp`
  - `draw_render_target_fill_available(...)`
  - `small_button(...)`
  - `checkbox(...)`
- `project/src/editor/Panels/SceneHierarchyPanel.cpp`
  - `tree_node(...)`
  - `button(...)`
- `project/src/editor/Panels/InspectorPanel.cpp`
  - `input_text(...)`
  - `drag_float3(...)`
  - `color_edit3(...)`
  - `combo(...)`
  - `checkbox(...)`
- `project/src/editor/Panels/AssetBrowserPanel.cpp`
  - `input_text(...)`
  - `combo(...)`
  - `begin_table(...)`
  - `input_text_multiline(...)`
- `project/src/editor/Panels/ConsolePanel.cpp`
  - `input_text(...)`
  - `combo(...)`
  - `begin_table(...)`

结论：

**活跃运行路径当前没有发现原生 ImGui 回退。**

### 3.2 B 类：活跃运行路径旁路基础设施，当前结果为“允许存在，但不直接承载原生 ImGui”

路径：

- `project/src/editor/Core/EditorContext.h`

结果：

- 这里只保留 `UIContext` 的前置声明与上下文指针
- 没有直接依赖原生 ImGui 头

结论：

**这是运行时路径的基础设施层，当前状态健康。**

### 3.3 C 类：历史参考路径，仍保留原生 ImGui，但已被排除出运行时构建

路径：

- `project/src/editor/ImGui/EditorImGuiLayer.cpp`
- `project/src/editor/ImGui/EditorStyle.cpp`

检出结果：

- `#include "imgui.h"`
- `ImGui::CreateContext()`
- `ImGui::GetIO()`
- `ImGui::DestroyContext()`
- `ImGui::NewFrame()`
- `ImGui::Render()`
- `ImGui::StyleColorsDark()`
- `ImGui::GetStyle()`

构建状态：

- `project/src/editor/premake5.lua` 明确通过 `removefiles` 排除了：
  - `ImGui/EditorImGuiLayer.h`
  - `ImGui/EditorImGuiLayer.cpp`
  - `ImGui/EditorStyle.h`
  - `ImGui/EditorStyle.cpp`

结论：

**这些文件属于历史参考实现，不属于当前运行时 Editor 路径。**

### 3.4 D 类：文档 / 草案参考，不属于运行时代码

路径：

- `docs/ImGuiLayer.InterfaceDraft.h`
- `docs/ImGuiLayer.InterfaceDraft.md`

结论：

**这些是设计参考，不是运行时代码，不纳入“活跃路径违规”统计。**

---

## 4. 当前已覆盖的 UIContext 能力

从本轮审计看，当前 `UIContext` 已足够支撑这些运行时 Editor 能力：

1. Dockspace host window 与 dock builder 布局
2. 主菜单与菜单项
3. 面板窗口 begin/end 生命周期
4. Viewport 离屏 RenderTarget 采样预览
5. 文本、换行文本、分隔线、按钮、复选框、下拉框
6. `input_text`
7. `input_text_multiline`
8. `drag_float3`
9. `color_edit3`
10. tree / table 基础能力

结论：

**P0 阶段运行时 Editor 的基础面板工作流，当前并没有被 UI 原语缺失卡死。**

---

## 5. 建议优先补的 UIContext 缺口

本节不是说“当前代码已经违规”，而是为了避免后续功能继续演进时重新引入原生 ImGui。

### P0：高优先级

1. Popup / Context Menu / Modal Dialog
   - 用途：
     - 资产浏览器右键动作
     - 删除确认
     - 保存确认
     - 错误弹窗 / 阻塞提示
   - 原因：
     - 这是 Editor 高频工作流能力
     - 如果没有，后续最容易诱发“临时回退到原生 ImGui popup”

2. Drag & Drop Payload API
   - 用途：
     - SceneHierarchy reparent
     - 资产拖入场景 / Inspector
     - 资源引用槽拖放
   - 原因：
     - 这是 Unity 风格编辑器的关键交互能力
     - 目前尚未在运行时路径里看到可直接承载该需求的 `UIContext` 抽象

3. 键盘快捷键 / 修饰键 / 输入捕获查询接口
   - 用途：
     - `Ctrl+S` / `Ctrl+Z` / `Ctrl+Y`
     - 焦点窗口专属输入
     - viewport 与 text input 的输入门控
   - 原因：
     - 当前菜单里已经展示快捷键文本，但真正的键盘查询与输入归属后续会成为刚需

### P1：中优先级

1. Table 排序 / 排序规格读取
   - 用途：
     - AssetBrowser 排序
     - Console 按时间 / 级别排序

2. 列表裁剪 / 大数据量可视化遍历支持
   - 用途：
     - 大型资产列表
     - 大量日志消息

3. Tree / List 多选与范围选择辅助
   - 用途：
     - 资源多选
     - SceneHierarchy 多实体选择

4. Toolbar / Segmented Toggle / Icon Button 统一语义封装
   - 用途：
     - Viewport 工具栏
     - Inspector 顶部操作条

### P2：较低优先级

1. Theme / Style 扩展面
   - 用途：
     - 后续 Editor 统一风格控制
   - 原因：
     - 当前不阻塞运行时路径收口
     - 只有真正需要运行时 Editor 自定义风格时才值得补

---

## 6. 结论

本轮审计的核心结论是：

1. 活跃运行路径当前未发现原生 `ImGui` 依赖回退
2. 原生 `ImGui` 依赖仍存在于 `project/src/editor/ImGui/**`，但它们已被 premake 排除，属于历史参考实现
3. 当前最需要做的不是“再迁移一遍现有路径”，而是提前补齐 Popup、DragDrop、快捷键输入查询这三类高优先级 `UIContext` 能力，避免后续功能演进时重新走回头路

---

## 7. 后续规则

1. 若未来 Editor 新功能需要通用立即模式原语，优先补到 `UIContext`
2. 若是 Editor 自身的策略、面板结构、工作区规则，继续保留在 Editor 层
3. 不要把历史参考文件里的原生 ImGui 代码重新带回活跃运行路径
