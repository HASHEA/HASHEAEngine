---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Module Spec: Editor

## 职责与边界

编辑器壳（`project/src/editor/`）：面板化 UI、选择/命令/undo-redo、场景编辑工作流、Gizmo、资产浏览。管编辑交互与编辑器状态；不管引擎运行时逻辑（Application/Scene/Render 属 engine），也不直接持有渲染资源——场景画面经 `ScenePresentationSubsystem` 离屏输出 + `UIContext` surface 显示。代码风格见 `docs/EditorCodeStyleGuide.md`。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `Editor.h/.cpp`、`EditorEntrypoint.cpp` | `Editor final : AshEngine::Application`，生命周期钩子内驱动 `EditorApplication` |
| `App/` | 组装层：`EditorApplication`（pimpl `EditorApplicationImpl`）、`EditorActionCoordinator`/`EditorActionRegistrar`、`PanelBootstrapper`、`SceneWorkflowCoordinator`、`EditorLogBridge`、`ViewportLayoutPersistence`、`ViewportPanelStateBridge` |
| `Core/` | 基础类型与契约：`EditorPanel` 基类、`EditorCommand`/`CompositeCommand`、`EditorEventBus`、`EditorContext`、`EditorFrameContext`、`EditorGizmoTypes.h`（GizmoMode/GizmoCoordinateSpace/GizmoPivotMode/GizmoSnapSettings/EditorGizmoState）、`EntityCommands`、快照/序列化工具、`I*` 接口族、`PanelDeps/`（各面板依赖注入结构） |
| `Shell/` | `MainMenuController`、`DockLayoutController`、`PanelManager`、`EditorStatusBarController`、`EditorCommandPaletteController` |
| `Panels/` | 面板实现与各自子目录（AssetBrowser/、Inspector/、SceneHierarchy/、ViewportPanel* 拆分文件） |
| `Services/` | 编辑器服务（下表） |
| `Widgets/` | 复用控件：`EditorActionWidgets`、`EditorButtonWidgets`、`EditorThemeColors`、`EditorTooltipWidgets`、`EditorTreeWidget`、`InspectorAssetPathWidgets`、`InspectorPropertyWidgets`、`ViewportAxisIndicator` |
| `ImGui/` | `EditorImGuiLayer`、`EditorStyle`——遗留的编辑器侧 ImGui 宿主，已被 premake `removefiles` 剔出构建（编辑器现全部经引擎侧 `UIContext` 渲染），仅作历史参考 |

## 公共接口

- 面板契约：`EditorPanel(id, title)`，虚函数 `OnAttach/OnDetach/OnUpdate/OnGui(const EditorFrameContext&)`；窗口经 `BeginPanelWindow/EndPanelWindow` 走 `UIContext`。全部面板：`AssetBrowserPanel`、`AssetPreviewPanel`、`ConsolePanel`、`InspectorPanel`、`SceneHierarchyPanel`、`ViewportPanel`。
- 命令与 undo/redo：`EditorCommand`（`GetLabel/Execute/Undo/TryMerge/GetSelectionAfterExecute/GetSelectionAfterUndo`，配合 `EditorCommandSelection`）、`CompositeCommand`；执行入队走 `CommandService`，历史栈在 `UndoRedoService`。
- 事件：`EditorEventBus`（类型索引的同步事件总线，`Subscribe<T>/Unsubscribe/Publish<T>`，非线程安全），事件类型在 `Core/EditorEventTypes.h`/`EditorEvents.h`。
- 上下文：`EditorContext`（SelectionService/SceneService/UIContext 指针）供命令执行；`EditorFrameContext` 携带 `AshEngine::UIContext*` 供每帧 GUI。
- Services（全部，位于 `Services/`）：`AssetDatabaseService`、`AssetPreviewService`、`CommandService`、`DragDropTransferService`、`EditorGizmoService`（含 `EditorGizmoMath/Transform/Viewport/Style/SelectionUtils`、`MoveScaleGizmoTool`、`RotateGizmoTool`）、`EditorIconService`（接口 `IEditorIconService`）、`EditorSessionStateService`、`EditorSettingsService`、`EditorShortcutService`、`EditorViewportCameraService`、`EditorViewportService`、`SceneService`、`SelectionService`、`SelectionOverlayRenderer`、`UndoRedoService`。

## 约束与不变式

- **Editor 只经 `UIContext` 与 Engine UI 交互，禁止直接使用 ImGui / Graphics API**：面板、控件、服务一律通过 `EditorFrameContext::pUiContext`（`ImGui/` 下的遗留桥接文件已被剔出构建）。
- 单一真源：活动场景状态唯一真源在 `SceneService`；Selection 只由 `SelectionService` 维护、undo/redo 栈只由 `UndoRedoService` 维护、viewport presentation 绑定只由 `EditorViewportService` 维护；快捷键文案与触发规则同一真源（`EditorShortcutService`）。
- 场景修改必须封装为 `EditorCommand` 经 CommandService 执行，保证 undo/redo 与选择一致性；面板间通信走 `EditorEventBus` 或 Services，不得互相直接引用。
- 场景 new/load/reload 的重置语义由 `SceneWorkflowCoordinator` 统一执行：清 Selection → 清 UndoRedo → 选中默认实体。primary viewport 唯一，viewport 共享状态只由 primary 发布。
- 稳定标识：action/panel/viewport 的 id 与 drag payload type 是持久化/交互契约，不得随意改名。
- 冻结快捷键（改动需用户确认）：Ctrl+N / Ctrl+R / Ctrl+S / Ctrl+Shift+R / Ctrl+Shift+A / Ctrl+Alt+A / F2 / Ctrl+Shift+P / Delete / Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z / F5，AssetBrowser 内容区 Enter/Backspace。
- 重构三原则：按面板/服务纵向深拆、不做横切层；`*Support` 文件只放无状态自由函数；主文件只做协调不塞业务。
- 改动定位：新功能先找最贴近的 panel/service/app/shell 文件落点，不默认塞 `EditorApplicationImpl.cpp`；panel 管 UI 与用户意图、service 管领域状态、app 管装配与生命周期、shell 管菜单/dock/状态栏，只在本层改。
- Editor 任务需要改 engine 侧代码时，仅限稳定接口/通用能力，且每处包裹 `// editor begin 修改原因：...` / `// editor end` 标记。
- 生命周期时序：`Editor` 构造函数只保存轻量配置，禁止 `HLog*` 与任何依赖 Engine 运行时的对象；`EditorApplication` 启动必须在 `Application::_on_startup()` 之后；关闭经 `_on_shutdown()` 对称执行，析构只做幂等兜底。
- 场景画面获取只走 `ScenePresentationSubsystem`（离屏 output + view binding + `get_ui_surface`）；overlay/拾取/统计用其 editor 扩展接口，不得直连渲染器。
- 依赖方向：Panels → Core/Services/Widgets；Shell 组织 Panels；App 组装一切；Core 不反向依赖上层。
- `EditorEventBus` 与各 Service 均假定主线程访问。

## 验证

对齐 `docs/VERIFY.md` "Editor 面板 / UI"行：

- 构建 + `run.bat editor`，手动过一遍改动路径（面板打开、交互、无报错日志）
- 涉及场景生命周期（打开/保存/reload）时升级为 `run.bat all Debug --smoke-test-seconds=5`

## 历史

无（早期 Editor 架构/进度/指引文档已删除，考古走 git 历史）。
