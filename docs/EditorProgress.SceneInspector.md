# Editor 模块进度：Inspector / Scene Shared Services

> 范围说明
>
> - 这份文档聚焦 `Inspector` 与它直接依赖的场景编辑共享服务。
> - `SceneHierarchyPanel` 的面板交互细节已经独立到 `docs/EditorProgress.SceneHierarchy.md`，这里不再重复记录。

## 1. 模块范围

- `project/src/editor/Panels/InspectorPanel.*`
- `project/src/editor/Services/SceneService.*`
- `project/src/editor/Services/SelectionService.*`
- `project/src/editor/Services/UndoRedoService.*`
- `project/src/editor/Core/EditorSelection.h`
- `project/src/editor/Core/EntityCommands.*`

## 2. 长期职责边界

负责：

- Inspector 面板
- 选择对象的属性展示与编辑
- Scene / Selection / UndoRedo 的共享编辑语义
- Inspector 与 scene lifecycle 的状态收口
- Inspector 复用的共享命令定义

不负责：

- SceneHierarchy 面板交互
- viewport 渲染语义
- 资产浏览器
- 控制台

## 3. 当前状态

- 状态：进行中
- 当前实现已具备最小可用 Inspector 工作流：
  - Entity 选择摘要
  - `Identity`
  - `Transform`
  - `Camera`
  - `Light`
  - `Mesh`
  - `Hierarchy` 只读信息
  - Asset 只读信息
- 当前实现已经补上一层组件编辑器骨架：
  - `InspectorPanel` 负责面板装配、选择态、Identity/Transform 与公共宿主能力
  - `Camera / Light / Mesh` 已拆到独立组件编辑器文件
  - 新增 `InspectorComponentEditor` 基类、`IInspectorComponentHost` 宿主接口、`InspectorPanelState` 共享状态结构
- 当前文档已按实际代码更新，不再沿用旧的“全部属性都已命令化”表述。

## 4. 当前已收口的编辑路径

### 4.1 已进入共享命令边界

- `Name`
  - 通过 `RenameEntityCommand`
  - 使用 draft + `Apply / Revert`
- `Transform`
  - 通过 `TransformEntityCommand`
  - 使用 draft + `Apply / Revert`

### 4.2 当前仍为直接写入

- `Camera`
  - 直接调用实体组件读写
  - `Add Camera / Remove Camera` 也是直接写入
- `Light`
  - 直接调用实体组件读写
  - `Add Light / Remove Light` 也是直接写入
- `Mesh`
  - 直接调用实体组件读写
  - `Add Mesh / Remove Mesh` 也是直接写入

### 4.3 当前共享命令的真实覆盖范围

- 已在 `project/src/editor/Core/EntityCommands.*` 中共享：
  - `RenameEntityCommand`
  - `TransformEntityCommand`
- 尚未从其他调用点完全收口到共享命令层：
  - `CreateEntity`
  - `ReparentEntity`
  - `DeleteEntity`
  - `Camera / Light / Mesh` 组件编辑命令

## 5. 当前实现约束

- Inspector 的 `Identity / Transform` 使用草稿缓冲，避免每次输入或拖拽都生成一条 undo 记录。
- 组件级 UI 不再继续堆到 `InspectorPanel.cpp`：
  - `project/src/editor/Panels/Inspector/CameraComponentEditor.*`
  - `project/src/editor/Panels/Inspector/LightComponentEditor.*`
  - `project/src/editor/Panels/Inspector/MeshComponentEditor.*`
- 共享组件编辑辅助逻辑已经收口到：
  - `project/src/editor/Panels/Inspector/InspectorComponentEditorSupport.*`
  - `project/src/editor/Panels/Inspector/InspectorPanelState.h`
- `SceneHierarchy` 相关的删除确认、reparent 交互、层级树操作，不在本文件重复维护，统一参考 `docs/EditorProgress.SceneHierarchy.md`。
- `scene changed` 路径当前要求统一收口到 `EditorApplication` 的 scene-change helper：
  - `startup scene load`
  - `new scene`
  - `reload active scene`

## 6. 已知问题 / 风险

- `Camera / Light / Mesh` 仍然绕过命令边界，和 `Name / Transform` 的交互语义不一致。
- `UndoRedoService::undo()` 当前无法向上层明确表达“撤销是否真正生效”。
- `UndoRedoService::redo()` 失败时会丢失当前命令，不属于安全失败语义。
- Inspector 中虽然已经有 section 注册/分发骨架，但 `boundary` 目前还只是元数据，没有真正驱动策略。

## 7. 后续建议

- 继续把 `Identity / Transform` 也迁到独立 section / editor 文件，彻底压缩 `InspectorPanel.cpp`。
- 优先把 `Camera / Light / Mesh` 编辑统一纳入命令边界。
- 把 `Create / Reparent / Delete` 从 `SceneHierarchyPanel.cpp` 继续收口到共享命令模块。
- 给 undo / redo 增加明确的失败语义，避免 UI 和日志误报成功。
- 如果 Inspector 继续扩展组件编辑器，优先沿用当前 section 分发骨架，不再回退到单体硬编码。

## 8. 测试与验收口径

- 这份文档当前对应的是代码级静态对齐，不代表已经完成完整运行时验收。
- 后续主线程至少应覆盖：
  - `Name / Transform` 的 apply / revert / undo / redo
  - `Camera / Light / Mesh` 编辑后的刷新行为
  - `new scene / load / reload` 后的 Inspector 状态归零
  - Selection 与 Inspector 的同步

## 9. 最近更新时间

- 2026-05-18
