# Editor Architecture And Requirements

> 当前 Editor 架构与需求基线。
>
> 只保留后续开发和重构仍需要遵守的信息；历史规划、已完成骨架、旧目录草案不再保留。

## 当前架构

| 层 | 目录 | 职责 |
| --- | --- | --- |
| App | `project/src/editor/App` | 启动、关闭、composition root、主循环编排、跨服务 workflow。 |
| Core | `project/src/editor/Core` | 命令、事件、选择、viewport/scene 类型、panel 基类、窄接口。 |
| Services | `project/src/editor/Services` | Scene、Selection、Undo/Redo、Command、Asset、Viewport、Gizmo、Settings 等领域状态。 |
| Shell | `project/src/editor/Shell` | Dockspace、菜单、命令面板、状态栏、panel 管理。 |
| Panels | `project/src/editor/Panels` | SceneHierarchy、Inspector、Viewport、AssetBrowser、Console 等工作区面板。 |
| Widgets | `project/src/editor/Widgets` | 可复用 UI 控件。 |
| ImGui | `project/src/editor/ImGui` | Editor UI 层桥接和主题。 |

更细的逐文件职责见 `docs/EditorFileResponsibilities.md`。

## 已成立的主线

- `Editor` 只桥接 Engine `Application` 生命周期。
- `EditorApplication` 是公开薄壳，内部走 PIMPL。
- `EditorApplicationImpl` 是 composition root，只做服务、shell、panel 装配和主循环编排。
- panel 通过 `PanelDeps` 接收明确依赖，不再默认吃整包 `EditorContext`。
- 编辑行为优先走 `EditorCommand` + `UndoRedoService`。
- 选择状态统一走 `SelectionService`。
- scene 文件生命周期统一由 `SceneService` + `SceneWorkflowCoordinator` 协调。
- viewport 状态统一走 `EditorViewportService`。
- gizmo 交互由 `EditorGizmoService` 协调，具体工具下沉到 `MoveScaleGizmoTool` / `RotateGizmoTool`。

## 长期边界

- Editor 只依赖 Engine `Function` / `Base` 的稳定接口。
- Editor 不直接依赖 Engine `Graphics`、RHI 后端、`KEnginePub`。
- Editor UI 活跃路径走 `UIContext`，不重新散落 `ImGui::` 调用。
- Engine 缺口先记录到 `docs/EngineInterfaceRequirements.md` / `docs/EditorToEngineGapChecklist.md`，不要默认侵入 Engine。
- 必须改 Engine 时，用 `// editor begin 修改原因：...` / `// editor end` 包住改动块。

## 当前需求基线

| 模块 | 当前目标 | 后续重点 |
| --- | --- | --- |
| Workspace / Viewport | 多 viewport、Scene/Game 视图、toolbar、interaction、layout persistence。 | 收口 RT 构建驱动，等待 Engine per-viewport overlay / GPU picking / stats。 |
| SceneHierarchy | 实体树、搜索、创建、重命名、改父级、删除、拖拽。 | 保持主 panel 协调者角色，子流程继续垂直拆分。 |
| Inspector | Transform/Camera/Light/Mesh 基础编辑，命令化提交。 | 扩展组件元数据、动态 Add/Remove Component、资源引用编辑。 |
| AssetBrowser | 资产扫描、目录树、内容区、搜索过滤、拖拽、预览联动。 | 合并过细 header 区域，收窄 view host，统一 Mesh/Model/Prefab 投放。 |
| Console / Status | 通知、日志、状态栏、dirty/action/session 信息。 | 补关键 workflow 日志，不把 console 变成业务依赖。 |
| Gizmo | Move/Rotate/Scale 工具、选择 overlay、viewport ray 交互。 | 等 Engine overlay 语义成熟后，把长期 3D 可视化迁出 2D overlay。 |

## 当前 Engine 缺口

只保留摘要，详细接口见 `docs/EngineInterfaceRequirements.md`。

- `AssetType::Mesh` 接入 `instantiate_asset(AssetId)`。
- 统一 scene drop point / ray-to-plane helper。
- Scene change event / lifecycle 语义。
- DebugDraw per-viewport / depth / xray 语义。
- GPU ID buffer picking。
- 组件元数据增强。
- 通用 Add/Remove Component facade。
- Viewport/render stats facade。

## 后续重构原则

- 深拆优先：按用户流程、工具模式、子窗口、modal 拆，不按空泛 MVC 层横切硬拆。
- 主文件只协调：`EditorApplicationImpl`、`ViewportPanel`、`SceneHierarchyPanel`、`AssetBrowserPanel` 不继续吞细节。
- `*Support.*` 只能放无状态小工具；一旦承载流程状态，就下沉到具体 feature 类。
- 窄接口只在真实边界使用，不全量 `I*` 化。
- 每轮只做一类改动，并保持 `build_editor.bat Debug x64` 可通过。
