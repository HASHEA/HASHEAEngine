# EditorApplication Refactor Temp Plan

> 临时执行文档。只记录后续可直接执行的重构步骤。

## 目标

- `EditorApplication` 只做公开薄壳。
- `EditorApplicationImpl` 只做 composition root，不承载 panel 业务细节。
- panel 使用 `Deps + EditorFrameContext`，不要重新依赖整包 `EditorContext`。
- 只在真实边界加接口，不做全量 `I*` 化。

## 已完成

- `EditorApplication.h` 已改成 PIMPL 薄头文件。
- `EditorApplicationImpl.h/.cpp` 已落地。
- `EditorApplicationImpl` 的主要 service/controller/manager 成员已改成 `std::unique_ptr`。
- `EditorApplicationImpl.h` 已大量使用前置声明。
- 已拆出 `EditorActionRegistrar`、`PanelBootstrapper`、`ViewportLayoutPersistence`、`ViewportPanelStateBridge`、`SceneWorkflowCoordinator`。
- 已拆出 `EditorActionCoordinator` 独立文件，不再留在 `EditorApplicationImpl.cpp` 内部。
- `EditorPCH.h` 已移除高层 engine/editor 依赖。
- 主要 panel 已有独立 `PanelDeps`。
- `PanelManager.Update()` 已不再接收 `EditorContext`。
- `PanelManager.DrawGui()` 已改为接收 `EditorFrameContext`。
- 已有窄接口：`IActionInvoker`、`IEditorCommandExecutor`、`INotificationSink`。
- 已补 panel action target 窄接口：asset browser / scene hierarchy action 不再通过 `dynamic_cast` 查找具体 panel。

## 当前问题

- `EditorApplicationImpl.cpp` 仍偏大，仍混有 action、shortcut、notification、panel 行为。
- `EditorActionCoordinator` 仍承担 panel action glue，后续可继续评估哪些 action 应该下沉到 panel 自注册。
- `EditorActionCoordinatorContext` 有 14 个成员引用，耦合面过宽，后续应按领域分组或拆子 coordinator。
- `EditorContext` 仍是残留桥接，Undo/Redo、UIContext、scene/selection 仍依赖它。
- `PanelBootstrapContext` 可以留在装配阶段，但不要扩散到 update/draw 路径。

## 下一步

### 1. 缩窄 `EditorContext`（原步骤 3 提前）

> 优先级最高。`EditorContext` 是后续所有改动的阻力源，先收口它可以降低其他步骤的耦合。

- 保持 panel update/draw 不接收 `EditorContext`。
- 新 panel 只用自己的 `PanelDeps`。
- 定义 `EditorCommandContext`（只含 SceneService + SelectionService），把 `UndoRedoService::Execute` 的签名改窄。
- `MakeEditorFrameContext` 改成直接取所需字段，不传整包 `EditorContext`。

完成标准：

- 新 panel 不 include `EditorContext.h`。
- 除 command 兼容层外，runtime 路径不传整包 `EditorContext`。
- `DrawGui` 路径不再依赖 `_editorContext` 整包传入。

### 2. 收尾头文件卫生

- 优先处理 panel/shell 头里的重依赖。
- `SceneHierarchyPanel.h`：评估 `EditorEventBindings.h`、`EditorTreeWidget.h` 是否有值类型成员阻止前置声明（当前有——`_eventBindings` 和 `_treeWidgetStateEntities` 是值类型成员，必须保留 include）。如果改为 `unique_ptr` 持有，则可以前置声明并下沉到 `.cpp`。
- `InspectorPanel.h`：评估是否能移除 `Scene.h` / `SceneComponents.h`；如果需要大拆 draft/state，先不要硬改。
- 暂缓拆 `EditorEventBus.h` 事件头，等重头 include 收口后再做。

完成标准：

- 改完 PCH 仍能编译。
- `.cpp` 显式 include 自己需要的实现头。
- 新增 include 不依赖 PCH 或传递 include。

### 3. 收口 action 边界

- 保留 app 级 action：theme、undo/redo、viewport open state、scene workflow。
- panel 专有 action 下沉到 panel 自己注册，或拆成窄 command target。
- 移除 `EditorActionCoordinator` 对具体 panel 的 `dynamic_cast`。
- 移除 app shell 对 panel 专有方法的直接调用。

完成标准：

- `EditorApplicationImpl.cpp` 不再 `dynamic_cast` 到 `AssetBrowserPanel` / `SceneHierarchyPanel`。
- panel 内部行为不再写进 app shell。

### 4. 处理 `EditorActionCoordinator`

- `EditorActionCoordinatorContext` 当前有 14 个引用成员，耦合面过宽。
- 评估按领域分组：scene 相关（SceneService, SelectionService, SceneWorkflowCoordinator）/ viewport 相关 / UI 相关，让 coordinator 持有 3-4 个子 context。
- 如果只剩少量 Impl 私有胶水，先留在 `.cpp`。
- 如果继续增长或需要测试，拆成 `EditorActionCoordinator.h/.cpp`。
- 拆文件时只搬代码，不改行为。

完成标准：

- `EditorApplicationImpl.cpp` 主要保留创建、注入、生命周期和每帧入口。

### 5. 谨慎补接口

- 暂缓 `IShortcutDispatcher`，除非 shortcut dispatch 需要独立复用或测试。
- 暂缓 `ISceneWorkflow`，除非多个模块需要依赖 scene workflow 边界。
- 不做全量 `ISceneService` / `ISelectionService` / `IUndoRedoService` / `IEditorViewportService`。

完成标准：

- 每个新增接口都有明确依赖隔离收益。

## 每轮约束

- 每轮只做一类改动。
- 每轮保持可编译。
- 不扩大 PCH 掩盖 include 问题。
- 不把 composition root 抽成 service locator。
- 不把 panel 私有行为继续塞进 `EditorApplicationImpl`。

## 验收

- `build_editor.bat Debug x64` 通过。
- Editor 能启动。
- 主菜单和快捷键可用。
- Undo/Redo 可用。
- Scene 新建、打开、保存、重载可用。
- Asset browser 基本导航可用。
- Scene hierarchy 创建、重命名、改父级、删除可用。
- Scene/Game viewport open state 可保存和恢复。
