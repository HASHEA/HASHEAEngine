---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Editor

## 职责与边界

编辑器壳（`project/src/editor/`）：面板化 UI、选择/命令/undo-redo、场景编辑工作流、Gizmo、资产浏览。管编辑交互与编辑器状态；不管引擎运行时逻辑（Application/Scene/Render 属 engine），也不直接持有渲染资源——场景画面经 `ScenePresentationSubsystem` 离屏输出 + `UIContext` surface 显示。代码风格见 `docs/editor/EditorCodeStyleGuide.md`。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `Editor.h/.cpp`、`EditorEntrypoint.cpp` | `Editor final : AshEngine::Application`，生命周期钩子内驱动 `EditorApplication` |
| `App/` | 组装层：`EditorApplication`（pimpl `EditorApplicationImpl`）、`EditorActionCoordinator`/`EditorActionRegistrar`、`PanelBootstrapper`、`SceneWorkflowCoordinator`、`EditorLogBridge`、`ViewportLayoutPersistence`、`ViewportPanelStateBridge` |
| `Core/` | 基础类型与契约：`EditorPanel` 基类、`EditorCommand`/`CompositeCommand`、`EditorEventBus`、`EditorContext`、`EditorFrameContext`、`EditorGizmoTypes.h`（GizmoMode/GizmoCoordinateSpace/GizmoPivotMode/GizmoSnapSettings/EditorGizmoState）、`EntityCommands`、快照/序列化工具、`I*` 接口族、`PanelDeps/`（各面板依赖注入结构） |
| `Shell/` | `MainMenuController`、`DockLayoutController`、`PanelManager`、`EditorStatusBarController`、`EditorCommandPaletteController` |
| `Panels/` | 面板实现与各自子目录（AssetBrowser/、Inspector/、SceneHierarchy/、ViewportPanel* 拆分文件）；Inspector component editor 由 registry 注册 |
| `Services/` | 编辑器服务（下表） |
| `Widgets/` | 复用控件：`EditorActionWidgets`、`EditorButtonWidgets`、`EditorThemeColors`、`EditorTooltipWidgets`、`EditorTreeWidget`、`InspectorAssetPathWidgets`、`InspectorPropertyWidgets`、`ViewportAxisIndicator` |
| `ImGui/` | `EditorImGuiLayer`、`EditorStyle`——遗留的编辑器侧 ImGui 宿主，已被 premake `removefiles` 剔出构建（编辑器现全部经引擎侧 `UIContext` 渲染），仅作历史参考 |

## 公共接口

- 面板契约：`EditorPanel(id, title)`，虚函数 `OnAttach/OnDetach/OnUpdate/OnGui(const EditorFrameContext&)`；窗口经 `BeginPanelWindow/EndPanelWindow` 走 `UIContext`。全部面板：`AssetBrowserPanel`、`AssetPreviewPanel`、`ConsolePanel`、`InspectorPanel`、`SceneHierarchyPanel`、`TerrainModePanel`、`ViewportPanel`。
- 命令与 undo/redo：`EditorCommand`（`GetLabel/Execute/Undo/TryMerge/GetSelectionAfterExecute/GetSelectionAfterUndo`，配合 `EditorCommandSelection`）、`CompositeCommand`；执行入队走 `CommandService`，历史栈在 `UndoRedoService`。常规命令由 `ExecuteCommand` 先执行再入栈；Engine 已原子完成的编辑（如 Terrain stroke 或 layer-stack mutation）只能走 `RecordExecutedCommand`，该路径不得再次调用 `Execute`，也不得进入开放 transaction（必须在命令仍由调用栈独占时同步回滚）。其三态结果明确区分 `Recorded`、`RolledBack`、`RollbackFailed`；历史存储失败时必须用同一命令的 `Undo` 回滚，调用方只有验证到 rollback generation 后才可继续发布，无法证明回滚时必须隔离该 authoring session，避免出现未被历史追踪的修改。
- 事件：`EditorEventBus`（类型索引的同步事件总线，`Subscribe<T>/Unsubscribe/Publish<T>`，非线程安全），事件类型在 `Core/EditorEventTypes.h`/`EditorEvents.h`。
- 上下文：`EditorContext`（SelectionService/SceneService/TerrainEditorService/UIContext 指针）供命令执行；`EditorFrameContext` 携带 `AshEngine::UIContext*` 供每帧 GUI。
- Services（全部，位于 `Services/`）：`AssetDatabaseService`、`AssetPreviewService`、`CommandService`、`DragDropTransferService`、`EditorGizmoService`（含 `EditorGizmoMath/Transform/Viewport/Style/SelectionUtils`、`MoveScaleGizmoTool`、`RotateGizmoTool`）、`EditorIconService`（接口 `IEditorIconService`）、`EditorSessionStateService`、`EditorSettingsService`、`EditorShortcutService`、`EditorViewportCameraService`、`EditorViewportService`、`SceneService`、`SelectionService`、`SelectionOverlayRenderer`、`TerrainBrushOverlayRenderer`、`TerrainEditorService`、`UndoRedoService`。`TerrainEditorService` 是 Terrain authoring session 的唯一 mutable owner；它异步加载 AssetDatabase snapshot，在 Editor `Update()` 中轮询，并把验证后的 `TerrainWorkingSet` 以值语义交给 UI-free `TerrainEditorSessionCore` 持有。`TerrainStrokeCommand` 只保存 asset/layer/sequence 与 Engine `TerrainEditPatch`；`TerrainLayerCommand` 只保存 asset/sequence、Engine `TerrainLayerStackPatch` 与变更前后选中层。两者 Undo/Redo 均回到 Function API 做身份、source state 和原子性校验；Editor 不实现第二套 patch codec 或回放器。
- Terrain stroke 在 Begin 时冻结二维 world metric 与 brush/layer 参数，Add 只按到达顺序保存 raw sample；End 对非空路径只调用一次 Engine `apply_terrain_brush_stroke`，并以 `RecordExecutedCommand` 记录一个已执行命令。Cancel、空路径或 Engine 原子失败不产生历史项；history rollback 失败会撤销待发布请求、把 preview 标为 `Failed` 并冻结该 session，直至重新打开 snapshot 或选择另一资产。每次 stroke/undo/redo 以更高 operation serial 覆盖旧的 deferred composition 请求；Phase 3 在下一次 Editor `Update()` 对最新 generation 的完整 dirty 集合同步 compose，并仅在 Engine publication 与 AssetDatabase publication 均成功后清 dirty、更新 immutable snapshot。发布失败会恢复原 component 指针和完整 dirty 集，保留已编辑 layer bytes 供后续重新调度；安全后台 compose 依赖 Phase 4 的 immutable/COW working-set capture，禁止为 8193² 地形按 stroke 复制整个 working set。
- Terrain layer action 支持 Add/Delete/Duplicate/Rename/Move/Visible/Opacity/Lock。Service 先调用 `apply_terrain_layer_stack_edit` 原子修改 working set，再用返回 patch 构造一个已执行命令；成功幂等操作返回空 patch，不推进 generation、不排 composition、也不进入历史。非历史 `SelectLayer` 只接受 working set 中存在的稳定 16-byte ID；BeginStroke 要求 intent layer、brush layer 与当前选中层三者一致。Layer command 显式保存变更前后选择，undo/redo 不从 vector index 猜测；锁定状态进入 preview 并阻止 BeginStroke，活动 stroke 期间拒绝切层和 layer action。记录失败沿同一 patch 回滚，并复用 stroke 的 generation、选择态、pending publication 验证与 quarantine 契约。
- `TerrainModePanel` 以稳定 `terrain_mode` / `Terrain` 身份注册、默认关闭并与 Inspector 共用右侧 dock。它只通过 `UIContext` 绘制 Manage/Sculpt/Paint/Layers；Asset Browser 的单一 Terrain 选择经类型过滤后提交 `SelectAsset`，普通 dirty 会话拒绝被另一资产替换，无法证明历史回滚的 quarantine 会话仍可通过明确选择另一资产离开。`TerrainAuthoringConfig` 由 `TerrainEditorService` 持有，面板只提交不可变 `ConfigureAuthoring` intent；半径、强度、falloff、spacing、64-bit seed、固定八层材质 lane 和当前稳定 layer ID 均由服务再次校验。`BeginStroke` 只接受与服务真源逐字段一致的 Sculpt/Paint 配置，活动 stroke 期间锁住 mode tab 和配置更新。Sculpt 根据 Additive/Alpha 层只显示兼容工具，Layers 的选择和操作以 16-byte ID 定位，vector index 只作为单次 Move 的目标位置；rename/opacity 草稿以 asset + layer ID 为身份，并在无关 generation 变化时保留尚未提交的本地输入。文件创建/导入/导出、保存、重载与 Optimize 在对应 service job/generation/conflict 切片完成前保持禁用并显示边界说明，不提交尚未实现的 intent。
- Terrain viewport 输入固定按 `Camera → Terrain → Gizmo → Selection` 仲裁。只有 canonical、primary、可接收输入的 Scene viewport 在 Sculpt/Paint 模式拥有未修饰 LMB；Alt/RMB/MMB、滚轮和 F 聚焦始终先交给相机，活动 stroke 遇到这些手势会取消。Terrain 接管 press 后锁存到物理 release，非 Ready 首击不会在按住期间状态转 Ready 后中途 Begin；Ready 首击同帧 Begin + 首采样，极短的 press+release 同帧还会立即 End，Ready release 先补末采样再 End。活动 stroke 命中 Outside 时结束已完成的有效段，Pending/Failed 时取消，随后同一 press 只继续屏蔽 gizmo/selection，避免重入后跨空白重采样连线；丢失 release edge 时，在 stroke 已不活动且物理 LMB 抬起后回收 latch。命中 entity 的 Terrain asset path 必须解析到当前 service 选中资产；sample index 乘 working-set spacing 转回 terrain-local 米，世界矩量保留 entity X/Z 非均匀缩放。Escape、Scene panel 折叠/关闭/Detach、content/presentation/primary/input 资格丢失会取消活动 stroke；严格的操作系统级全应用失焦仍属于后续多 viewport focus 聚合边界。旧 pending GPU pick 和框选状态在 Terrain 接管时清除；Sculpt/Paint ownership 期间禁用 W/E/R 与 Move/Scale/Rotate，Manage/Layers 保持原 gizmo 行为。
- `TerrainEditorPreviewState.query_status` 只表示 authoring session 的加载/可编辑/quarantine 状态；实时光标命中保存在独立 `viewport` 子状态，包含 cursor query status、经归一化的 world center/normal、service-owned brush radius、选中 Terrain 的 scene entity ID 和显式 `has_world_position`。只有当前选中 asset、Sculpt/Paint mode 和 canonical primary Scene 可更新该状态；Outside、模式/主视口/输入资格丢失及 Scene panel 生命周期结束会清空。Pending/Failed 可保留同一 session 上一个合法 anchor 用于状态着色，但 foreign Terrain hit 不得发布新 anchor。该分离禁止 mouse Outside/Pending 反向禁用 Layers 或覆盖 session quarantine 语义，并为 world-space overlay 提供可查询地表的稳定 entity 身份。
- `TerrainBrushOverlayRenderer` 只消费 service preview、当前 immutable published snapshot、preview entity 的现行 axis-aligned world transform 与 Scene binding。它生成固定 64 段 world-XZ 圆并逐点查询 snapshot 高度；只连接相邻 Ready 端点，缺失 Component 留出 gap，不跨空白。session 非 Ready、Outside/无 anchor、非法 transform/binding、实体资产换绑或空线集均零提交；Ready 绿、cursor Pending 琥珀、cursor Failed/locked 红。Canvas 必须在 helper overlay 的当帧 clear 之后、2D box selection 之前，仅为 canonical primary 且可输入的 Scene 调用；提交只走 Function `SceneOverlayLine`/`submit_scene_overlay`，不访问 Graphics。
- Inspector：Camera/Light/Mesh/Environment/Particle/Terrain 六类 component editor 经 `InspectorComponentEditorRegistry` 注册，Name/Transform 由固定 section 绘制；Particle 草稿可编辑容量、模拟、颜色/尺寸、混合和完整 uint32 seed，提交为 `SetParticleComponentCommand`。Terrain 草稿编辑 `.AshTerrain` 资产、可见性、投射/接收阴影及固定八层材质覆盖，提交为 `SetTerrainComponentCommand`；空/错误类型资产、无效材质覆盖或 Terrain 层级中非零旋转/非正缩放会阻止提交。Terrain 的 entity snapshot 使用专用序列化保留八层覆盖，复制、删除恢复和 undo/redo 不丢字段。
- readiness smoke：`Editor::_get_automation_readiness` 要求 bootstrap、UI renderer、AssetDatabase refresh 与 viewport presentation 同步成功；最终 ready 仍由 Application 的当前帧全 scene packet + asset epoch + present 公共契约证明。Editor 不生成 golden。

## 约束与不变式

- **Editor 只经 `UIContext` 与 Engine UI 交互，禁止直接使用 ImGui / Graphics API**：面板、控件、服务一律通过 `EditorFrameContext::pUiContext`（`ImGui/` 下的遗留桥接文件已被剔出构建）。
- 单一真源：活动场景状态唯一真源在 `SceneService`；Selection 只由 `SelectionService` 维护、undo/redo 栈只由 `UndoRedoService` 维护、viewport presentation 绑定只由 `EditorViewportService` 维护、Terrain mutable authoring session 只由 `TerrainEditorService` 维护；快捷键文案与触发规则同一真源（`EditorShortcutService`）。
- Gizmo 视觉与命中投影必须同源：Scene viewport 的中央 gizmo 与右上角 XYZ 指示器使用当前相机的同一 view basis；Move/Scale 平面手柄的世界四角必须逐点做透视投影，绘制和命中共用所得凸四边形。相机上下文不可用或投影退化时隐藏对应视觉，禁止回退到 identity 朝向或屏幕轴对齐矩形。
- 场景修改必须封装为 `EditorCommand` 经 CommandService 执行，保证 undo/redo 与选择一致性；面板间通信走 `EditorEventBus` 或 Services，不得互相直接引用。
- Particle 必须进入 entity snapshot 的复制/删除/撤销恢复路径。连续属性编辑只在 command 前后状态结构连续时合并；跨 saved checkpoint 不合并，最终状态等于初始状态时不得生成空历史项。
- 场景 new/load/reload 的重置语义由 `SceneWorkflowCoordinator` 统一执行：清 Selection → 清 UndoRedo → 选中默认实体。primary viewport 唯一，viewport 共享状态只由 primary 发布。
- 稳定标识：action/panel/viewport 的 id、drag payload type 与 Terrain layer 的 16-byte ID 是持久化/交互契约；layer command 和选择不得持久化 vector index。
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
- 涉及场景生命周期（打开/保存/reload）时升级为 `run.bat all Debug --smoke-test-seconds=120`
- Terrain authoring 还需运行 focused stroke/layer/viewport/brush-overlay 行为测试、幂等历史门禁、稳定选择、锁定与 rollback/quarantine 契约，以及 Debug/Release 全量 `RunTests.bat`；brush overlay 改变 scene presentation output，必须额外运行双后端 `RunRenderGate.bat`。

## 历史

- [SDD-2026-07-10-gpu-particles](../../sdd/SDD-2026-07-10-gpu-particles.md)（粒子 Inspector、snapshot 与 undo/redo）
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)（Editor bootstrap/UI/资产库 readiness）
- [SDD-2026-07-12-editor-gizmo-projection](../../sdd/SDD-2026-07-12-editor-gizmo-projection.md)（方向指示器 camera basis 与 Move/Scale 平面手柄透视投影）
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)（Terrain authoring、稳定图层 patch 与 Phase 3/4 边界）
