# AshEngine Editor 架构改造方案

> 文档状态：**当前主线改造方案**
>
> 这份文档用于指导 `project/src/editor` 后续的结构化重构与能力扩展。
>
> 适用范围：
> - Editor 应用框架
> - Panel / Service / Command / UndoRedo
> - Scene 编辑链路
> - Workspace / Dockspace / Viewport
> - 未来 Asset Editor、Prefab Editor、更多工具面板的扩展路径
>
> 不直接覆盖：
> - Engine 底层渲染架构
> - RHI / Graphics / Renderer 内部实现
> - 运行时 Gameplay 逻辑

---

## 1. 背景与目标

当前编辑器主线已经具备以下基础能力：

- `EditorApplication` 负责启动、上下文注入、菜单、面板、布局与运行时同步
- `EditorPanel` 作为统一面板基类
- `SceneService` / `SelectionService` / `CommandService` / `UndoRedoService` 提供基础编辑能力
- `SceneHierarchy`、`Inspector`、`AssetBrowser`、`Viewport` 已形成基础工作流
- 通用树控件 `EditorTreeWidget` 已落地，可作为后续控件抽象的样板

这说明当前架构方向是正确的，已经从“散乱的临时 UI 拼接”进入了“可以演进的编辑器骨架”阶段。

但同时也出现了几个典型的中期风险：

- `EditorApplication` 开始承担过多职责，正在向 God Object 演化
- `SceneService` 同时承担场景操作、快照恢复、JSON 编解码、默认模板初始化，边界过宽
- `EditorContext` 作为宽依赖包，导致面板对全局状态拥有过高访问权限
- `CommandService` 过于轻量，尚不足以支撑快捷键、命令面板、上下文菜单、toolbar 复用
- 快捷键字符串声明与真实按键检测分离，存在文案和行为漂移风险
- `UndoRedo` 的选择态模型偏窄，未来会限制多选、资产编辑器和图编辑器扩展
- `ViewportService` 同时管理 UI 配置、渲染绑定、持久化和运行态句柄，职责混杂
- 缺少统一的编辑器事件系统，跨模块状态同步主要依赖直接调用、局部回调和每帧轮询

本方案的目标不是推翻现有结构，而是在保留当前可运行主线的前提下，逐步把编辑器改造成：

1. **边界清晰**
2. **职责单一**
3. **方便扩展**
4. **便于多人并行**
5. **便于测试与回归验证**

---

## 2. 改造总原则

### 2.1 保持功能连续可用

重构必须采用“分阶段、小步提交、每阶段可编译可运行”的方式进行，禁止一次性大重写。

### 2.2 先拆编排层，再拆领域层

优先处理 `EditorApplication` 这种高耦合编排点；在编排层职责清晰后，再拆 `SceneService` 等领域服务。

### 2.3 UI 与编辑领域状态分离

面板只负责展示和交互，不直接承担复杂业务编排；复杂状态变更应下沉到服务、命令或会话协调器。

### 2.4 缩窄依赖面，而不是继续扩大 `EditorContext`

以后新增能力时，优先新增专用依赖对象、窄接口或 facade，不再继续往 `EditorContext` 里堆指针。

### 2.5 命令系统是主编辑链路核心，不走旁路

凡是会改变可撤销编辑状态的操作，原则上都应通过 Command / UndoRedo 进入主链路。

### 2.6 服务层按“领域”拆，不按“面板”拆

例如：

- 层级编辑属于 `SceneHierarchyService`
- 场景快照属于 `SceneSnapshotService`
- 面板只调用这些服务，不自己复制逻辑

### 2.6.1 coordinator 只做编排，不吞领域

如果引入 coordinator / workflow 类，其职责必须限制在：

- 跨多个 service 的流程编排
- 生命周期与错误处理
- 状态重置顺序控制

它不应重新长成第二个 `SceneService` 或第二个 `EditorApplication`。

### 2.7 重构优先保持行为等价

除非某一阶段任务明确写了“允许调整产品行为”，否则默认要求：

- 新旧实现对外行为等价
- 交互顺序等价
- 持久化结果等价
- Undo/Redo 语义等价
- 面板可见性与布局恢复行为等价

这里的“等价”不是要求内部实现完全一样，而是要求用户能感知到的结果、状态流转和操作顺序不变。

### 2.8 引入新抽象时先做旁路承接，再做主路切换

所有架构改造默认采用以下顺序：

1. 新增承接类 / facade / adapter
2. 保持旧 API 不变，把旧逻辑转发到新类
3. 逐步迁移调用点
4. 验证行为未变
5. 最后删除旧入口

禁止跳过“承接层”直接把旧实现整体撕开重拼。

### 2.9 架构重构优先级低于功能正确性

如果出现以下冲突，默认优先保证现有功能正确：

- 架构更优雅，但会改变交互行为
- 目录更整洁，但会提高回归面
- 抽象更纯粹，但会导致调试链路变差
- 类边界更漂亮，但需要同时修改大量调用点

本轮重构的核心目标是“可持续演进”，不是“追求一次性最优结构”。

### 2.10 与 Engine 既有边界保持一致

Editor 架构改造不能逆向破坏已经确定的 Engine / Editor 边界，尤其是 scene-driven viewport 这一条主线。

当前必须继续遵守：

- `Scene` / `Game` viewport 走 `ScenePresentationSubsystem`
- UI 层只消费 `UISurfaceHandle`
- 不把 `RenderScene` / `SceneView` / `SceneRenderer` / viewport `RenderTarget` 重新暴露回 Editor 上层

这条约束与 `docs/ScenePresentationSubsystemGuide.md`、`docs/EditorDeveloperGuide.md` 保持一致。

### 2.11 共性抽取按层次进行，不做“大一统工具箱”

“把通用功能抽出来复用”和“把所有东西塞进一个公共模块”不是一回事。

后续抽共性时，必须先分清三层：

- 共享基础设施
  - 例如 action 呈现、drag payload、ID/label/icon 解析、通用验证与格式化
- 共享业务能力
  - 例如 scene hierarchy 合法性校验、selection snapshot 恢复、undo label 生成
- 共享 UI 组件
  - 例如 tree、toolbar toggle、modal scaffold、empty state、badge、overlay card

强约束：

- 不允许把“任意地方可能有用”的代码统统塞进 `Utils`
- 不允许让 UI 组件反向持有 service locator
- 不允许为了复用而抽出一个比原逻辑更难理解的抽象
- 同一共性只有在“至少两处稳定复用”或“明确是战略基础设施”时才提升为公共模块

“一个地方管理所有 UI 组件”是合理目标，但这里的“一个地方”应理解为：

- 一个清晰的目录与命名体系
- 一套统一的风格 token 与使用约束
- 一个明确 owner 的组件库

而不是一个运行时的、无边界的 `UIManager` 大对象。

---

## 3. 当前架构主要问题

## 3.1 当前功能冻结基线

后续重构期间，以下能力视为**冻结基线**。除非任务卡明确声明允许调整，否则默认不得改变其用户侧行为。

### 3.1.1 启动与 Workspace

- Editor 能启动进入主工作区
- 主菜单、Dockspace、各面板窗口能正常显示
- `Window -> Reset Layout` 可恢复默认布局
- Scene / Game 两个 viewport 的 panel open state 能保留并恢复

### 3.1.2 Scene 工作流

- 新建场景后能创建默认场景内容
- 重载场景后 SceneHierarchy、Inspector、Selection 同步刷新
- 保存场景仍沿用当前保存路径决策
- 切场景后 Undo/Redo 清空，Selection 重置为默认实体

### 3.1.3 SceneHierarchy

- 单击选择实体
- Rename / Reparent / Delete 入口可用
- 拖拽支持 `Before / After / Into`
- root append drop slot 可用
- 拖拽时自动展开与插入线行为保持当前语义
- 图标选择逻辑保持当前语义映射

### 3.1.4 Inspector

- Identity / Transform / Camera / Light / Mesh 可编辑
- Apply / Revert 工作正常
- 编辑通过命令链进入 Undo/Redo
- 切换 Selection 后草稿同步逻辑不变

### 3.1.5 AssetBrowser

- 目录树显示正常
- List / Icons 视图切换正常
- Search / Type Filter / Reset Filters 正常
- 右键菜单与双击激活逻辑正常
- 目录树与右侧内容区联动正常

### 3.1.6 Viewport

- Scene / Game viewport surface 正常显示
- 请求尺寸与同步尺寸逻辑正常
- toolbar / overlays / stats 行为不变
- viewport primary 选择逻辑不变

### 3.1.7 编辑命令链

- 所有当前已支持的场景编辑操作仍进入 Undo/Redo
- `Undo -> Redo` 后结果与执行前一致
- transaction 未完成时不能执行普通 undo/redo
- 已存在的 command merge 语义不得意外丢失

### 3.1.8 持久化与配置

- 不修改 Scene 文件格式
- 不修改已有 EditorSettings 字段语义
- 不修改 ViewportLayout.json 的现有字段语义
- 不修改面板标题与持久化依赖的窗口名，除非先做迁移方案

### 3.1.9 快捷键与输入语义

以下快捷键在未明确立项调整前，默认视为冻结行为：

- `Ctrl+N`：New Scene
- `Ctrl+R`：Reload Scene
- `Ctrl+S`：Save Scene
- `Ctrl+Shift+R`：Reset Layout
- `Ctrl+Shift+A`：Add Root
- `Ctrl+Alt+A`：Add Child
- `F2`：Rename
- `Ctrl+Shift+P`：Reparent
- `Delete`：Delete Selected
- `Ctrl+Z`：Undo
- `Ctrl+Y` / `Ctrl+Shift+Z`：Redo
- `F5`：Refresh Assets

以下局部导航行为也视为冻结行为：

- `AssetBrowser` 内容区 `Enter`：打开 / 激活当前选中项
- `AssetBrowser` 内容区 `Backspace`：返回上级目录

此外，当前输入抑制语义默认保持不变：

- 当 `ImGuiIO::WantTextInput` 为真时，一般全局快捷键不触发
- 除非某快捷键被明确声明允许在文本输入态触发

## 3.2 重构核心不变量

以下内容在整个重构周期内视为**不变量**。

### 3.2.1 单一真源不变量

- 当前活动场景只能有一个真源对象
- 当前 Selection 只能由 `SelectionService` 维护
- 当前 Undo/Redo 栈只能由 `UndoRedoService` 维护
- 当前 viewport presentation 状态只能由 `EditorViewportService` 维护

禁止在 panel 本地缓存“另一份权威状态”并长期驱动 UI。

### 3.2.2 UI 路径不变量

- Editor 主 UI 继续走 `UIContext`
- 不允许为了“重构方便”重新散落原生 `ImGui::` 主路径
- 控件内部可使用必要 `ImGui` 细节，但不能绕开 `UIContext` 重建 editor-host 模式

### 3.2.3 命令链不变量

- 任何会改变 Scene 可编辑状态的用户操作，都必须能纳入命令链
- 不允许为临时重构跳过命令链直接改 Scene，再计划“后面补 Undo”
- 如果某操作当前走命令链，重构后仍必须走命令链

### 3.2.4 快捷键真源不变量

- action 展示用的 shortcut 文案与真实触发规则必须最终来自同一真源
- `can_execute` 与快捷键是否可触发必须最终来自同一真源
- 不允许长期保留“菜单显示一套字符串，按键检测另一套硬编码”的结构
- 局部面板快捷键也必须有清晰 owner，不能无序散落

### 3.2.5 事件发布真源不变量

- 领域状态变化的跨模块通知，最终必须由统一事件系统或明确的专用观察者发布
- 不允许多个模块各自手动广播同一语义事件
- 不允许 panel 互相直接调用来模拟跨模块事件传播

### 3.2.6 标识稳定性不变量

- action id 保持稳定
- panel id 保持稳定
- drag payload type 保持稳定
- 关键窗口 title 在无迁移方案前保持稳定
- icon enum 已暴露并被调用的 id 在无迁移方案前保持稳定

### 3.2.7 生命周期不变量

- service 初始化顺序不得被隐式改变
- panel attach/detach 顺序必须可追踪
- shutdown 顺序必须与资源依赖关系一致
- 任何新类接管生命周期后，都要明确其 owner

## 3.3 重构期间禁止事项

以下事项在“架构改造提交”中默认禁止。

### 3.3.1 禁止在同一提交中同时做三件事

- 大规模目录搬迁
- 公共 API 改签名
- 用户侧行为调整

这三类事情同时发生时，回归定位成本会急剧上升。

### 3.3.2 禁止借重构顺手改产品行为

例如：

- 改默认选中实体策略
- 改保存路径策略
- 改拖拽插入规则
- 改 Inspector Apply/Revert 逻辑
- 改 panel title / docking 名称
- 改现有快捷键触发规则
- 改文本输入态下的快捷键抑制规则

这些改动必须单独立项，而不是顺带混入重构。

### 3.3.3 禁止破坏持久化兼容

以下文件默认不能在架构重构中随意改格式：

- Scene 文件
- `product/config/editor/EditorSettings.json`
- `product/config/editor/ViewportLayout.json`
- `product/config/editor/imgui.ini`

如果必须改：

- 先写迁移方案
- 说明兼容策略
- 说明回退策略

### 3.3.4 禁止同时保留两套长期有效实现

允许短期 adapter / forwarding wrapper，但禁止：

- 新旧主实现长期并存
- panel A 走新服务，panel B 永久停在旧逻辑
- “先这样放着以后再统一”无限期拖延

每个过渡层都必须有明确移除目标。

## 3.4 迁移守则

### 3.4.1 旧入口先保留，内部转发

例如把 `SceneService` 的快照能力迁到 `SceneSnapshotService` 时，应先：

- 保留原入口
- 原入口内部转调新服务
- 调用点逐步迁移
- 最后删除旧入口

### 3.4.2 一次只改变一个主责任归属

例如在“拆 `EditorApplication`”阶段，只处理：

- 面板管理归属
- 布局归属
- 菜单归属

但不要同时把 Scene 领域逻辑也一起改走。

### 3.4.3 允许临时 facade，不允许临时分叉

允许新增：

- `PanelManager`
- `SceneSnapshotFacade`
- `EditorShell`

不允许新增：

- `NewSceneServiceButActuallyAnotherSceneService`
- `TemporaryUndoRedo2`
- `LegacyCompatibleMaybeManager`

命名和职责都要清晰，否则只是在制造第二套技术债。

### 3.4.4 删除旧代码前必须满足两个条件

1. 全工程调用点已迁完
2. 已执行本阶段回归清单

任一条件不满足，不允许删旧入口。

## 3.5 `EditorApplication` 过重

当前 `EditorApplication` 同时负责：

- 服务初始化
- 上下文注入
- 面板创建与销毁
- 菜单绘制
- Action 注册
- 场景切换与默认选择
- Dockspace 构建
- Viewport 布局持久化
- 日志转发
- Runtime ScenePresentation 同步

这会导致：

- 任何功能扩展都容易改到同一个类
- 改动冲突频繁
- 单元测试困难
- 生命周期错误更难定位

## 3.6 `SceneService` 边界过宽

当前 `SceneService` 混合了：

- 基础实体创建、删除、重命名、reparent
- 场景加载/保存
- Entity 快照捕获与恢复
- 组件 JSON 编解码
- 默认场景模板创建

问题在于它不再是“场景编辑服务”，而是“所有和 Scene 相关的东西都往里放”。

## 3.7 `EditorContext` 是宽权限全局入口

`EditorContext` 当前相当于一个可变 service locator。它提升了开发速度，但也带来：

- 面板间隐藏耦合
- 依赖不透明
- 面板测试必须拉整套上下文
- 后续多文档、多编辑器实例时更难拆

## 3.8 Action 模型过于原始

当前 action 只有：

- `id`
- `label`
- `callback`

缺少：

- `can_execute`
- `is_checked`
- `shortcut`
- `scope`
- `category`
- `tooltip`
- `icon`
- `command palette` 所需元数据

这会导致菜单、toolbar、快捷键、右键菜单各自重复写判断逻辑。

## 3.9 当前快捷键体系割裂

当前快捷键相关实现已经暴露出明确的架构问题：

- `CommandService` / `EditorAction` 保存了 shortcut 字符串，仅用于菜单与上下文菜单展示
- 真正的按键检测仍硬编码在 `EditorApplication::handle_global_shortcuts()`
- 局部快捷键又散落在各 panel 内，例如 `AssetBrowserPanel` 的 `Enter` / `Backspace`
- 快捷键启用条件没有进入统一 action 模型，仍由调用点各自判断
- 当前实现直接依赖 `ImGui::IsKeyChordPressed` / `ImGui::IsKeyPressed`

这意味着当前“快捷键系统”并不是系统，而是三层拼接：

1. action 元数据
2. editor application 中央硬编码
3. panel 局部硬编码

风险包括：

- 菜单显示与真实触发规则漂移
- 新增快捷键时需要同时改多处
- 无法统一处理 scope、焦点、文本输入态抑制
- 后续做快捷键设置、命令面板和 toolbar 复用时成本高

## 3.10 Selection 模型过于单一

当前 Undo/Redo 只内建恢复 `Entity` 选择。

未来一旦出现以下能力，就会卡住：

- Asset 选择与激活
- 多选
- Prefab 层级编辑
- Material / Graph 节点编辑
- 独立 Asset Editor

## 3.11 Viewport 层状态未分层

当前 `EditorViewportService` 同时持有：

- 视口实例状态
- 展示配置
- 渲染同步状态
- 输出句柄
- 绑定句柄
- 持久化信息

未来如果加入：

- 新建辅助 viewport
- 相机锁定 / 预览模式
- Play In Editor / Simulate
- 独立渲染设置

这个类会快速膨胀。

## 3.12 当前缺少统一事件系统

目前编辑器并没有一套统一的编辑器事件总线。

现状更接近下面三类混用：

- 直接调用 service / panel 方法
- `SelectionService` 这种局部 observer 回调
- 每帧轮询状态刷新 UI

这在当前规模下能工作，但会在以下场景开始失稳：

- 需要多个模块同时响应场景切换
- 需要统一响应 dirty 状态变化
- 需要在 action / selection / undo 栈变化时刷新多个入口
- 需要避免 panel 间互相直接调用
- 需要未来引入 command palette、状态栏、通知中心

因此，后续应补一套**轻量、类型安全、主线程内、以编辑器状态事件为主**的事件系统。

---

## 4. 目标架构

建议把 Editor 主体拆成下面几层。

```text
Editor
└── EditorApplication
    ├── EditorBootstrap
    ├── EditorShell
    │   ├── WorkspaceHost
    │   ├── MainMenuController
    │   ├── DockLayoutController
    │   └── PanelManager
    ├── EditorSession
    │   ├── SceneWorkflowCoordinator
    │   ├── SelectionService
    │   ├── UndoRedoService
    │   ├── ActionRegistry
    │   ├── EditorEventBus
    │   └── ViewportService
    ├── Domain Services
    │   ├── SceneDocumentService
    │   ├── SceneHierarchyService
    │   ├── SceneSnapshotService
    │   ├── SceneTemplateService
    │   └── AssetDatabaseService
    └── Panels
        ├── SceneHierarchyPanel
        ├── InspectorPanel
        ├── AssetBrowserPanel
        ├── ViewportPanel
        └── ConsolePanel
```

---

## 5. 目标分层职责

## 5.1 `EditorApplication`

保留为最外层生命周期入口，只负责：

- 调用 bootstrap
- 驱动 update / gui / render sync
- 处理 shutdown 顺序

它不再直接承担菜单细节、布局细节、场景切换细节、action 注册细节。

## 5.2 `EditorBootstrap`

负责一次性启动与关闭编排：

- 发现 workspace root
- 初始化 settings
- 初始化 icon / assets / scene / viewport
- 创建 EditorSession
- 创建 EditorShell

这是“启动流程组装器”，不承担运行时业务。

## 5.3 `EditorShell`

负责 Editor UI 外壳层：

- 主菜单栏
- Workspace Host Window
- Dockspace
- 面板注册与创建
- 默认布局与布局恢复

它只处理“编辑器壳子长什么样”，不负责具体编辑功能。

建议拆出：

- `MainMenuController`
- `DockLayoutController`
- `PanelManager`

## 5.4 `EditorSession`

负责“当前编辑会话”的运行态总协调，是未来支持多文档、多编辑上下文的关键入口。

建议职责：

- 持有当前打开的 SceneDocument
- 暴露 selection / undo / command / viewport 等会话级服务
- 响应场景切换、重新加载、保存、重置状态
- 向 UI 层提供只读 session 状态

当前阶段可以先只有一个 session，但结构上要按“未来可多 session”来设计。

### 5.4.1 `EditorSession` / `SceneWorkflowCoordinator` / `SceneDocumentService` 边界

这三类对象必须严格区分职责，避免重构过程中重新长出新的 God Object：

- `EditorSession`
  - 当前编辑会话的 owner
  - 持有会话级服务
  - 维护“谁活着、谁依赖谁”
- `SceneDocumentService`
  - 只负责 Scene 文档本身的 new/load/save/path/dirty/display name
  - 不负责 UI reset、selection reset、undo 清理顺序
- `SceneWorkflowCoordinator`
  - 只负责跨 service 的场景工作流编排
  - 例如 `new scene -> clear selection -> clear undo -> rebuild default selection -> publish scene changed`
  - 不吞并文档 IO 细节，也不接管 hierarchy 领域规则

如果后续觉得 `SceneWorkflowCoordinator` 太重，应继续拆其流程，而不是重新把逻辑塞回 `EditorSession` 或 `SceneDocumentService`。

## 5.5 `SceneDocumentService`

负责当前场景文档本身：

- 新建场景
- 加载场景
- 保存场景
- 路径管理
- dirty 状态
- 文档标题和显示名

注意：它负责“文档”，不负责 hierarchy 操作细节。

## 5.6 `SceneHierarchyService`

负责实体层级编辑语义：

- create entity
- delete entity
- rename entity
- reparent entity
- sibling insert / move
- hierarchy legality校验

SceneHierarchyPanel、InspectorPanel、Command 都应调用它，而不是分别拼自己的规则。

## 5.7 `SceneSnapshotService`

负责编辑器级快照能力：

- capture entity snapshot
- restore entity snapshot
- 组件序列化 / 反序列化
- subtree 恢复

它是 Undo/Redo 和复制/粘贴的底层能力服务，不应继续塞在 `SceneService` 中。

## 5.8 `SceneTemplateService`

负责默认场景模板与预置内容：

- 创建空场景模板
- 创建默认 Root / Camera / Light
- 后续扩展到更多模板类型

这样默认场景初始化就不会继续污染 `SceneService`。

## 5.9 `ActionRegistry`

用于替换当前轻量 `CommandService`。

建议 action 描述至少包含：

- `id`
- `label`
- `category`
- `tooltip`
- `icon`
- `primary_shortcut`
- `secondary_shortcut`
- `scope`
- `allow_when_text_input`
- `execute`
- `can_execute`
- `is_checked`
- `is_visible`

作用：

- 菜单、toolbar、右键菜单复用同一套 action
- 后续做 Command Palette 不需要重新建一套数据结构
- 快捷键展示和真实触发规则可以直接落在 action 层

### 5.9.1 `ActionRegistry` 的最低落地要求

第一阶段不要求一步到位做成“完整命令系统平台”，但至少要满足：

- action id 继续沿用当前稳定字符串，例如 `file.new_scene`、`edit.undo`、`selection.rename`
- 菜单显示的 shortcut 文案与真实按键绑定来自同一份 action / shortcut 元数据
- `can_execute` 成为 enable 判断真源，菜单、toolbar、快捷键触发不再各自复制一份判断
- 允许 action 没有快捷键，但不允许“有展示 shortcut 却没有对应真实绑定 owner”
- action 的 `execute` 仍是主动命令入口，事件系统不替代 action 执行

### 5.9.2 `ActionRegistry` 与快捷键的 owner 边界

建议把“动作”与“输入绑定”拆成两个概念，但统一由同一 owner 管理：

- `ActionRegistry`
  - 保存 action 元数据与执行/可执行判断
- `EditorShortcutService`
  - 保存 `action_id -> key chord / scope / 输入约束`
  - 负责分发输入到 action

两者可以物理上是两个类，也可以先由一个类短期承载；但从职责上必须保证：

- action 负责“这是什么动作、什么时候能执行”
- shortcut 负责“什么输入在什么上下文触发这个动作”
- 菜单、命令面板、右键菜单、toolbar 都只读 action / shortcut 真源，不再自行硬编码字符串

### 5.9.3 快捷键冻结兼容要求

在本轮重构内，`ActionRegistry` 升级时必须保持以下兼容：

- 现有 action id 不改名
- 不引入用户自定义快捷键 UI
- 不修改当前 `Ctrl+N`、`Ctrl+S`、`Ctrl+Z`、`F5` 等冻结绑定
- 不修改 `Ctrl+Y / Ctrl+Shift+Z` 双重 redo 兼容语义
- 不修改“有选中实体时才允许 `scene.create_child` / `selection.rename` / `selection.reparent` / `selection.delete`”的启用规则

## 5.10 `UndoRedoService`

继续保留，但要升级为“会话级编辑历史服务”。

后续增强方向：

- 支持更通用的 selection snapshot
- 支持 command label 展示
- 支持长操作 transaction 包装
- 支持 inspector 拖动过程的 begin/commit/cancel transaction

## 5.11 `EditorViewportService`

保留，但内部拆两层模型：

- `ViewportModel`
  - UI 配置
  - open state
  - toolbar / stats / overlays
  - requested size
- `ViewportRenderBinding`
  - output handle
  - binding handle
  - pending sync
  - synced extents

这样以后扩展 play/simulate/camera lock 时不会把渲染绑定和 UI 状态搅在一起。

重构该服务时还必须保持下列边界不变：

- `ViewportPanel` 继续只看 UI-facing viewport state 与 `UISurfaceHandle`
- service 内部继续持有 scene presentation output / binding
- 不重新把 `RenderScene` / `SceneRenderer` / viewport `RenderTarget` 暴露回 editor 上层

## 5.12 `EditorEventBus`

建议补一个轻量事件系统，作为 editor 内部状态变化的统一发布入口。

第一版只需要覆盖少量高价值事件：

- `ActiveSceneChanged`
- `SelectionChanged`
- `UndoStackChanged`
- `ViewportLayoutReset`
- `ActiveDocumentDirtyStateChanged`

要求：

- 主线程内
- 类型安全
- 不承担跨线程消息系统职责
- 不替代 command / undo / selection service，只负责状态变更通知

### 5.12.1 `EditorEventBus` 的目标定位

`EditorEventBus` 不是“什么都往里发”的全局消息箱，而是 editor session 内部的**状态变更通知总线**。

它的目标很明确：

- 减少 panel 与 panel 之间的直接互调
- 减少多个模块同时监听同一状态时的手写回调散落
- 让 `SelectionChanged`、`ActiveSceneChanged`、`UndoStackChanged` 这类高频协同状态有统一广播点

它不负责：

- 命令执行
- request / response 流程
- 跨线程任务调度
- 替代所有 per-frame UI 轮询

### 5.12.2 第一批建议事件类型

建议第一批事件限制在少量高价值、语义清晰、发布 owner 明确的类型：

- 文档 / 场景类
  - `ActiveSceneChanged`
  - `SceneReloaded`
  - `ActiveDocumentDirtyStateChanged`
- 选择类
  - `SelectionChanged`
- 层级类
  - `EntityCreated`
  - `EntityDeleted`
  - `EntityRenamed`
  - `EntityReparented`
  - `SceneHierarchyChanged`
- Undo/Redo 类
  - `UndoStackChanged`
  - `TransactionStateChanged`
- Viewport 类
  - `PrimaryViewportChanged`
  - `ViewportPresentationChanged`
  - `ViewportLayoutReset`
- Action / Shortcut 类
  - `ActionRegistryChanged`
  - `ShortcutBindingsChanged`

第一阶段不要求全部同时落地，但必须按这个方向约束命名和 owner。

### 5.12.3 事件发布约束

`EditorEventBus` 必须遵守以下硬约束：

- 一个语义事件只能有清晰、唯一或可枚举的 owner 发布方
- 事件只表达“已经发生了什么”，不表达“请别人去执行什么”
- 事件处理默认同步、主线程内完成
- 事件回调里应避免再次发起大范围级联修改；如必须修改，应明确记录允许的重入边界
- 不允许 panel 为了刷新别的 panel 状态而伪造领域事件

### 5.12.4 与现有 `SelectionService` observer 的关系

当前 `SelectionService::subscribe/unsubscribe` 可以视为事件系统的局部前身。

迁移时建议：

- 短期允许 `SelectionService` 内部继续维护自己的 listener
- 但同时由 `SelectionService` 成为 `SelectionChanged` 的唯一发布 owner
- 后续消费者逐步从 `SelectionService::subscribe` 迁到 `EditorEventBus`
- 待调用点迁完后，再决定是否完全移除 `SelectionService` 自带 observer 接口

## 5.13 共享基础设施层

建议补一层明确的共享基础设施，用于沉淀“多个模块都需要，但又不属于某一个 panel 或某一个 service 私有职责”的能力。

这层不是万能工具箱，而是受控的公共能力层。

### 5.13.1 适合进入共享基础设施的内容

结合当前代码现状，优先建议抽以下几类：

- `EditorActionPresenter`
  - 统一根据 action id 绘制 menu item / toolbar button / context action
  - 统一读取 label / shortcut / enable 语义
- `EditorShortcutTextFormatter`
  - 把 chord 规则格式化为展示文案
  - 保证菜单、tooltip、命令面板显示一致
- `EditorDragDropPayloads`
  - 统一 payload type 常量、编码/解码 helper、基本校验逻辑
  - 避免 payload type 字符串散落
- `EditorEntityPresentation`
  - 统一实体类型到图标、label、辅助描述的映射
  - 避免 `SceneHierarchy`、Inspector、未来 Outliner 各自写一套
- `EditorValidation`
  - 统一输入合法性、命名规则、index clamp、可见错误文案生成
- `EditorEmptyState` / `EditorStatusText`
  - 统一“service unavailable”“surface unavailable”“暂无内容”这类基础状态展示文案和样式

### 5.13.2 不应该进入共享基础设施的内容

以下内容不应以“共性抽取”为名提前塞进公共层：

- 只在一个 panel 使用一次的 helper
- 直接修改 scene / selection / undo 的业务逻辑
- 面向某个 panel 私有交互的临时状态
- 为了节省几行代码而引入的泛型模板套壳

### 5.13.3 共享基础设施的接口原则

这层代码应遵守：

- 接口尽量无状态，或只持有极窄状态
- 不依赖具体 panel 类
- 不反向依赖 `EditorContext` 大包
- 输入输出可枚举、可测试
- 优先返回值对象，而不是隐式修改外部状态

### 5.13.4 当前建议的第一批抽取项

根据当前面板代码重复度，建议第一批优先抽下面这些：

1. action 呈现与调用 helper
2. toolbar toggle button 样式 helper
3. confirm / rename / apply-cancel modal scaffold
4. empty state / unavailable state 展示 helper
5. drag/drop payload 常量与解析 helper

这些都是复用面明确、收益高、回归面相对可控的第一批目标。

## 5.14 `EditorWidgetLibrary`

建议正式引入一套统一的 editor UI 组件库，但其定位必须是“组件定义与样式中心”，不是“掌管所有 UI 运行时状态的中央控制器”。

### 5.14.1 `EditorWidgetLibrary` 的目标

它负责：

- 提供 editor 复用控件与组合控件
- 统一 spacing、padding、icon gap、颜色 token、交互反馈
- 为 panel 提供更高层的 UI building block
- 让视觉与交互约束集中管理

它不负责：

- 接管 panel 生命周期
- 持有业务领域状态
- 直接执行 scene / asset / undo 逻辑
- 变成新的 service locator

### 5.14.2 组件分层建议

建议把组件库分成三层：

- Primitive Widgets
  - ToggleButton
  - IconLabelRow
  - StatusBadge
  - EmptyState
  - SearchBar
  - OverlayCard
- Composite Widgets
  - ActionMenuSection
  - ConfirmDialog
  - RenameDialogScaffold
  - BreadcrumbBar
  - FilterToolbar
  - PropertySectionHeader
- Domain Widgets
  - `EditorTreeWidget`
  - 未来的 `PropertyGrid`
  - 未来的 `CommandPaletteList`

原则上越靠上层，越要明确输入参数与 callback 边界，避免把领域逻辑偷偷卷进去。

### 5.14.3 当前代码中可见的可抽组件信号

从当前代码可以直接看出几类重复模式：

- `ViewportPanel`、`InspectorPanel`、`AssetBrowserPanel` 都有相似的“选中态按钮着色”逻辑
- `SceneHierarchyPanel` 已经出现多组结构相近的 modal：Rename / Reparent / Delete
- `SceneHierarchyPanel` 与 `AssetBrowserPanel` 都在手写 action shortcut 文案读取与菜单 invoke
- 多个 panel 都有 “xxx unavailable.” 这类空态 / 降级展示
- `EditorTreeWidget` 已经证明“复杂交互控件抽公共层”是可行路径

这说明组件库方向是对的，而且已经具备试点基础。

### 5.14.4 组件库的 owner 方式

“由一个地方管理所有 UI 组件”建议按下面方式落地：

- 目录 owner：统一落在 `editor/UI` 或 `editor/Widgets`
- 风格 owner：统一由 `EditorStyleCatalog` 或 `EditorThemeTokens` 管理
- 组件 API owner：由 editor 架构层维护命名、参数规范、使用示例
- 运行时状态 owner：仍由具体 panel 持有

也就是说：

- 组件库管理“定义、样式、接口”
- panel 管理“业务状态、数据来源、交互结果”

### 5.14.5 组件库接口约束

后续组件库中的控件建议遵守以下接口规范：

- 尽量通过参数传入文案、状态、icon、action id、callback
- 不直接读取 `EditorContext`
- 不在组件内部偷偷访问某个 service
- 允许使用 `UIContext`
- 复杂组件如 tree / property grid 可以持有自己的 widget state，但必须显式传入

### 5.14.6 本轮建议优先建设的 UI 组件

建议先做一套“小而硬”的 editor ui kit，而不是一上来做几十个组件：

- `EditorToggleButton`
- `EditorActionMenu`
- `EditorConfirmDialog`
- `EditorEmptyState`
- `EditorOverlayCard`
- `EditorToolbar`
- `EditorBreadcrumbBar`
- 保留并继续增强 `EditorTreeWidget`

### 5.14.7 通用组件复用优先原则

后续 UI 开发默认遵守下面这条优先级：

1. 功能和界面几乎相同，优先复用通用组件
2. 只存在稳定的小差异，优先扩展现有通用组件
3. 功能模型、状态流或布局骨架差异明显，则新建组件

这里的“几乎相同”至少应满足：

- 交互流程基本一致
- 组件状态模型基本一致
- 布局骨架基本一致
- 不需要为了复用而增加大量特判分支

如果不满足这几个条件，就不应该强行复用。

### 5.14.8 修改通用组件的安全规则

当某个新需求想复用已有组件时，必须先回答一个问题：

“为了支持这个新需求，对通用组件的改动会不会损害其他已在使用它的调用方？”

只有在答案明确为“不会”时，才允许修改通用组件本身。

建议判断标准：

- 默认行为保持不变
- 现有调用方不需要被迫跟改
- 不引入破坏原有视觉和交互的一揽子副作用
- 不把组件 API 扩成难以理解的条件分支集合

如果做不到，就不要改通用组件主干。

### 5.14.9 通过开关和状态扩展组件的适用边界

使用开关、状态、配置参数扩展通用组件，是允许且推荐的，但只适用于“稳定维度上的可枚举差异”。

适合用开关 / 状态扩展的情况：

- 是否显示 icon
- 是否显示 shortcut
- 是否为危险操作样式
- 是否允许多选
- 视觉密度、尺寸、padding、row 高度等展示差异

不适合继续加开关的情况：

- 新需求引入了完全不同的交互流程
- 新需求要求组件依赖某个特定 panel/service 的业务数据
- 新需求需要把旧组件内部逻辑拆成大量互斥分支
- 新需求已经让组件的命名和职责变得不准确

一旦出现这些信号，应停止给旧组件继续打补丁。

### 5.14.10 无法良好融合时直接新建组件

如果当前需求与已存在组件差异较大，无法在不损害既有调用方的前提下融合，就应直接新增组件。

这不是“放弃复用”，而是保护已有公共组件的边界。

允许新增组件的典型场景：

- 布局结构明显不同
- 交互步骤明显不同
- 状态机明显不同
- 领域语义明显不同

新增组件时仍应尽量复用下层 primitive 或 shared helper，但不要强行复用上层 composite 组件。

### 5.14.11 UI 通用组件与业务逻辑分离原则

UI 组件必须尽量通用，并与特有逻辑、特有数据分开。

组件层负责：

- 展示
- 基础交互
- 局部视觉状态
- 通过参数接收数据和 callback

panel / controller / presenter 层负责：

- 业务数据组织
- service 调用
- command 执行
- 领域校验
- 跨模块流程编排

明确禁止：

- 组件内部直接访问 `EditorContext`
- 组件内部直接操作 `SceneService` / `UndoRedoService` / `SelectionService`
- 组件内部偷偷持有某个业务模块的长期状态

### 5.14.12 通用组件准入与分叉规则

后续是否进入公共组件库，建议按下面规则判断：

- 至少已有两个或预计很快会有两个高相似度消费点
- 抽出后接口比调用点内联实现更清晰
- 组件职责能用一句话说清楚
- 不会把业务逻辑一并带入公共层

如果不满足这些条件：

- 先放在当前模块内实现
- 等第二个真实场景出现后，再决定是否提升为通用组件

这条规则的目的不是保守，而是避免过早抽象。

只要这批组件立住，后续 `AssetBrowser`、`SceneHierarchy`、`Viewport` 的很多 UI 代码就能明显收敛。

---

## 6. 依赖规则

## 6.1 面板层依赖规则

Panel 可以依赖：

- 自己的 `Deps` / facade
- `UIContext`
- 只读 session 状态
- action 调用入口

Panel 不应该：

- 直接知道所有 service
- 直接改 unrelated service
- 复制领域规则

### 建议形态

不要再让每个 panel 直接拿完整 `EditorContext`，改为：

```cpp
struct SceneHierarchyPanelDeps
{
    AshEngine::UIContext* ui = nullptr;
    SelectionService* selection = nullptr;
    SceneHierarchyService* hierarchy = nullptr;
    UndoRedoService* undo_redo = nullptr;
    EditorIconService* icons = nullptr;
};
```

这样依赖范围一眼可见。

## 6.2 服务层依赖规则

服务之间允许有限协作，但要避免双向耦合。

建议依赖方向：

```text
Panel -> Facade/Deps -> Session/Services -> Engine Scene
Command -> Session/Services -> Engine Scene
Shell -> ActionRegistry / PanelManager / DockLayoutController
```

禁止：

- `SceneService` 反向依赖具体 Panel
- `ViewportService` 依赖具体面板类
- `UndoRedoService` 依赖具体面板状态对象

## 6.3 引擎边界规则

Editor 继续遵守现有原则：

- 优先依赖 Engine `Function/` 层公开接口
- 不在 Editor 侧扩散对 Graphics/RHI 的直接耦合
- 编辑器特有语义放在 Editor 层，不继续污染 `UIContext`

## 6.4 owner / lifecycle matrix

后续正式拆类时，建议先按下表维护 owner 关系，避免改着改着生命周期漂移。

| 对象 | owner | 创建时机 | 销毁时机 | 主要依赖 | 备注 |
|------|-------|----------|----------|----------|------|
| `EditorApplication` | `Editor` | editor bootstrap | editor shutdown | Engine `Application` | Editor 最外层生命周期入口 |
| `EditorBootstrap` | `EditorApplication` | initialize 前后的一次性流程 | initialize 完成后可释放或降为辅助对象 | settings / workspace / session / shell | 只做启动编排 |
| `EditorSession` | `EditorApplication` | bootstrap 完成后 | `EditorApplication::shutdown()` 中按依赖顺序销毁 | scene document / selection / undo / actions / viewport / events | 会话级 owner |
| `EditorShell` | `EditorApplication` | session 就绪后 | shutdown 中按反向依赖销毁 | `UIContext` / panel manager / dock layout / menu | UI 外壳层 |
| `PanelManager` | `EditorShell` | shell init | shell shutdown | panel deps / frame context | panel 生命周期唯一 owner |
| `DockLayoutController` | `EditorShell` | shell init | shell shutdown | `UIContext` / viewport persistence | layout 唯一 owner |
| `MainMenuController` | `EditorShell` | shell init | shell shutdown | action registry / session state | 不承载业务执行细节 |
| `SceneDocumentService` | `EditorSession` | session init | session shutdown | engine scene / serializer | 文档真源 |
| `ActionRegistry` | `EditorSession` | session init | session shutdown | session state / command callbacks | action 元数据与触发真源 |
| `EditorEventBus` | `EditorSession` | session init | session shutdown | session state / UI observers | 只做状态变更通知 |
| `EditorWidgetLibrary` | `EditorShell` 或独立 UI 子模块 | shell init 前后均可，优先在 UI 可用后构建 | shell shutdown | `UIContext` / theme tokens / icon service | 管理组件定义与样式，不持有业务状态 |
| `EditorStyleCatalog` | `EditorWidgetLibrary` 或 `EditorShell` | shell init | shell shutdown | theme preset / spacing tokens / color tokens | UI 设计 token 真源 |
| `EditorViewportService` | `EditorSession` | session init | session shutdown | `ScenePresentationSubsystem` / scene / settings | 持有 viewport presentation 真源 |
| `EditorIconService` | `EditorApplication` 或 `EditorShell` | UI 可用后 | UI 关闭前 | `UIContext` / icon assets | 必须晚于 `UIContext` 注销 |
| `UIContext` | Engine `Application` | engine startup | engine shutdown | engine window / graphics backend | Editor 只借用，不拥有 |

---

## 7. 关键对象重构建议

## 7.1 用 `EditorFrameContext` 替换“宽运行态上下文”

建议把当前 `EditorContext` 分成两类：

### 1. `EditorFrameContext`

每帧动态信息：

- `UIContext*`
- `gui_renderer_ready`
- 主视口只读状态

### 2. 专用依赖对象

按面板 / 控制器注入：

- `SceneHierarchyPanelDeps`
- `InspectorPanelDeps`
- `AssetBrowserPanelDeps`
- `ViewportPanelDeps`

这样可以避免“所有人共享一个大包”。

## 7.2 引入 `SceneDocument`

建议新增文档模型：

```cpp
struct SceneDocument
{
    std::filesystem::path path;
    std::string display_name;
    bool dirty = false;
    AshEngine::Scene scene;
};
```

即使当前只支持一个场景文档，也建议提前引入 `SceneDocument` 语义，原因是：

- 场景文档与编辑会话是不同概念
- 后续支持多场景 Tab 时迁移成本更低
- save / reload / dirty / title 逻辑更集中

## 7.3 Action 系统升级

当前 `CommandService` 建议演进为：

```cpp
struct EditorActionDesc
{
    std::string id;
    std::string label;
    std::string tooltip;
    std::string category;
    std::string shortcut_text;
    std::function<void()> execute;
    std::function<bool()> can_execute;
    std::function<bool()> is_checked;
    std::function<bool()> is_visible;
};
```

后续所有入口统一通过 action：

- main menu
- toolbar
- context menu
- future command palette
- future shortcut map

## 7.4 Selection 升级为可扩展快照

建议把当前 `EditorSelection` / `EditorCommandSelection` 扩成可扩展模型：

- `None`
- `Entity`
- `Asset`
- `MultiEntity`
- `GraphNode`
- `Custom`

Undo/Redo 不应只知道 `EntityId`，而应知道“如何恢复选择”。

可选方案：

- 值对象式 `SelectionSnapshot`
- 小型 command 式 selection restore object

## 7.5 事务化编辑流

Inspector 拖拽、viewport gizmo 拖拽、批量改属性都应该统一走：

- `begin_transaction`
- 多次更新
- `commit_transaction`
- 或 `cancel_transaction`

目标是让编辑体验一致，避免不同面板各自发明一套临时行为。

## 7.6 输入与快捷键真源建议

快捷键体系后续应收口到“action 描述 + 真实触发规则”同源的模型。

建议最低目标：

- shortcut 文案从 action 描述读取
- shortcut 触发规则也从 action 描述或与之绑定的 key chord 描述读取
- `can_execute` 与 shortcut 是否可触发使用同一套判断
- 区分 `global shortcut` 与 `panel-scoped shortcut`
- 文本输入态抑制规则集中在一处，而不是各 panel 自己判断

在该体系真正落地前，文档中提到的现有 shortcut 冻结项都应视为“待兼容行为”，不能在重构中顺手改掉。

### 7.6.1 当前实现诊断

当前快捷键实现实际分成三层：

1. `CommandService` / `EditorAction`
   - 保存 label 与 shortcut 字符串
   - 主要用于菜单展示
2. `EditorApplication::handle_global_shortcuts()`
   - 负责全局快捷键真实检测
   - 当前硬编码了 `Ctrl+N`、`Ctrl+R`、`Ctrl+S`、`Ctrl+Shift+R`、`Ctrl+Z`、`Ctrl+Y` 等规则
3. 局部 panel 自己读输入
   - `AssetBrowserPanel` 直接检测 `Enter` / `Backspace`
   - 后续若别的 panel 继续这样做，体系会继续碎裂

这套结构短期可用，但存在三个核心问题：

- 展示 shortcut 与真实触发规则可能漂移
- 焦点 / scope / 文本输入态抑制没有统一 owner
- 新入口如 command palette、toolbar、状态栏想消费 action 状态时，只能继续复制判断逻辑

### 7.6.2 目标模型

建议补两个结构：

```cpp
enum class EditorShortcutScope
{
    Global,
    Workspace,
    PanelFocused,
    ContentFocused
};

struct EditorShortcutBinding
{
    std::string action_id;
    ImGuiKeyChord primary_chord = 0;
    ImGuiKeyChord secondary_chord = 0;
    EditorShortcutScope scope = EditorShortcutScope::Global;
    bool allow_when_text_input = false;
};

struct EditorActionDesc
{
    std::string id;
    std::string label;
    std::string tooltip;
    std::string category;
    std::string shortcut_text;
    std::function<void()> execute;
    std::function<bool()> can_execute;
    std::function<bool()> is_checked;
    std::function<bool()> is_visible;
};
```

设计重点不是字段长什么样，而是以下关系必须成立：

- `shortcut_text` 必须由 `EditorShortcutBinding` 生成或与其同源
- `EditorShortcutService` 只分发到 `action_id`
- 实际是否允许触发由 action 的 `can_execute` 决定
- panel 不直接持有“另一套长期有效快捷键真源”

### 7.6.3 scope 与解析顺序建议

建议把 shortcut scope 先收敛为四类，不要一开始做复杂优先级图：

- `Global`
  - 无需特定 panel 焦点即可触发
  - 例如 `file.save_scene`、`edit.undo`
- `Workspace`
  - 需要 editor workspace 处于有效焦点内
- `PanelFocused`
  - 只有指定 panel 或其根窗口 focused 时允许触发
  - 例如 `AssetBrowser` 的局部导航
- `ContentFocused`
  - 除 panel focused 外，还要求内容区有有效选中项或 hover / active 区域

建议解析顺序：

1. 先过滤文本输入态
2. 再过滤 scope
3. 再调用 action `can_execute`
4. 最后执行 action

这样可以把当前 scattered 的 `WantTextInput`、`has_selected_entity`、`IsWindowFocused(...)` 逻辑逐步收拢。

### 7.6.4 与当前行为保持等价的硬约束

快捷键体系重构期间，以下行为必须严格保持：

- `EditorApplication::handle_global_shortcuts()` 当前已支持的冻结绑定保持不变
- `Ctrl+Y` 与 `Ctrl+Shift+Z` 都继续触发 redo
- `AssetBrowser` 的 `Enter` / `Backspace` 继续保留当前内容区导航语义
- `WantTextInput` 为真时，一般全局 shortcut 不触发
- `selection.*` 与 `scene.create_child` 的 enable 条件继续依赖当前选中实体存在与否

本阶段明确不做：

- 用户自定义快捷键界面
- 配置文件持久化 shortcut
- 多 stroke 组合键
- 宏命令 / chord 序列编辑器

### 7.6.5 迁移步骤建议

建议按下面顺序迁移，避免一次性推翻现有输入路径：

#### 第一步：补 action 可执行语义

- 给当前 action 增加 `can_execute`
- 菜单 enable 判断尽量改为直接读 action
- 先不改真实按键检测 owner

#### 第二步：抽 `EditorShortcutService`

- 把 `EditorApplication::handle_global_shortcuts()` 内的 binding 表迁到新服务
- `EditorApplication` 仍可保留一个薄转发入口
- 保证旧行为一字不差后，再删旧数组定义

#### 第三步：迁移 panel-local shortcut

- 以 `AssetBrowserPanel` 的 `Enter` / `Backspace` 为第一个试点
- 允许 panel 提供 focus / active predicate
- 但真实 shortcut 绑定注册改走统一服务

#### 第四步：为命令面板 / toolbar / context menu 统一读 action 元数据

- 所有入口显示一致的 label / shortcut / enable 语义
- 验证“新增一个快捷键只改一处元数据”已经成立

## 7.7 事件系统设计建议

### 7.7.1 总体原则

编辑器事件系统建议做成：

- 轻量
- 类型安全
- 主线程同步
- notification-only

推荐 API 形态：

```cpp
class EditorEventBus
{
public:
    template<typename TEvent>
    EditorEventSubscription subscribe(std::function<void(const TEvent&)> handler);

    void unsubscribe(EditorEventSubscription token);

    template<typename TEvent>
    void publish(const TEvent& event);
};
```

这类 API 足够支撑当前 editor 规模，不需要一上来引入复杂优先级、异步队列或跨线程桥接。

### 7.7.2 推荐首批事件

建议把首批事件限定为下面这些“高价值且易定义 owner”的类型：

- `ActiveSceneChanged`
  - owner：`SceneWorkflowCoordinator` 或 `SceneDocumentService`
- `SceneReloaded`
  - owner：`SceneWorkflowCoordinator`
- `ActiveDocumentDirtyStateChanged`
  - owner：`SceneDocumentService`
- `SelectionChanged`
  - owner：`SelectionService`
- `UndoStackChanged`
  - owner：`UndoRedoService`
- `TransactionStateChanged`
  - owner：`UndoRedoService`
- `EntityRenamed` / `EntityReparented` / `SceneHierarchyChanged`
  - owner：`SceneHierarchyService`
- `ViewportPresentationChanged` / `PrimaryViewportChanged`
  - owner：`EditorViewportService`
- `ShortcutBindingsChanged`
  - owner：`EditorShortcutService`

### 7.7.3 明确禁止的错误用法

以下几种用法应在文档层面直接禁止：

- 用事件替代命令
  - 例如发布 `DeleteSelectedRequested` 让别人自己删
- 用事件在 panel 之间传 UI 碎片状态
  - 例如某 panel hover 了哪个按钮、展开了哪个 tree node
- 用事件兜底不清楚 owner 的状态同步
  - 如果不知道谁该发，说明领域边界还没理清
- 一个语义被多个模块重复发布
  - 例如 `SelectionChanged` 同时从 panel 和 service 广播

### 7.7.4 什么时候不该上事件

以下场景继续优先使用直接调用或每帧读取：

- 直接命令执行
- 单一 panel 内部状态
- 每帧都会自然刷新的纯展示数据
- 需要立即拿返回值的查询

也就是说，事件系统不是为了“替代函数调用”，而是为了解决“多个模块需要感知同一已发生状态变化”的问题。

### 7.7.5 与现有代码的兼容迁移路线

建议分三步：

#### A. 建总线，不急着全量接入

- 新增 `EditorEventBus`
- 只接最少事件类型
- 不强迫所有模块立刻迁移

#### B. 先迁移高价值观察点

- `SelectionChanged`
- `ActiveSceneChanged`
- `UndoStackChanged`
- `ViewportPresentationChanged`

优先让菜单状态、状态栏、辅助 panel 这类天然多消费者的地方开始订阅。

#### C. 清理重复观察者

- 当 `SelectionService::subscribe` 等局部 observer 已没有必要保留时，再统一收口
- 对于仍适合局部 observer 的场景，也要明确其仅作为 service 内部机制，而不是第二套对外事件系统

## 7.8 共享模块抽取实施方案

### 7.8.1 先做盘点，再做抽取

共享模块最怕“边写边感觉像共性，于是立即抽象”。正确顺序应是：

1. 盘点当前重复代码
2. 判断重复是否稳定
3. 确认 owner 和边界
4. 先抽无争议基础能力
5. 再让第二个、第三个调用点接入

### 7.8.2 当前建议的抽取优先级

#### 第一优先级：低风险高复用

- action menu / context action 呈现
- toggle button 样式与状态按钮
- empty state / unavailable state
- drag payload helper

#### 第二优先级：中风险高收益

- modal scaffold
- overlay info card
- icon + label row / item renderer
- breadcrumb / filter toolbar

#### 第三优先级：延后处理

- property grid
- inspector 字段编辑器族
- 命令面板列表
- 可配置 toolbar schema

第三优先级不要现在就做大抽象，否则很容易把当前节奏拖慢。

### 7.8.3 推荐目录形态

建议新增或逐步整理为：

```text
editor/
├── Shared/
│   ├── Actions/
│   │   ├── EditorActionPresenter.*
│   │   └── EditorShortcutTextFormatter.*
│   ├── DragDrop/
│   │   └── EditorDragDropPayloads.*
│   ├── Presentation/
│   │   ├── EditorEntityPresentation.*
│   │   └── EditorEmptyStateText.*
│   └── Validation/
│       └── EditorValidation.*
├── UI/
│   ├── Components/
│   ├── Composite/
│   ├── Styling/
│   └── Widgets/
```

这样“共享功能”和“共享 UI”各有归属，不会混成一个大杂烩。

### 7.8.4 抽取时的硬约束

- 共享模块不得依赖具体 panel
- 共享模块不得直接拥有长期 editor 全局状态
- 若一个抽象只有一个消费者，默认仍放回原模块，除非它是明确战略基础设施
- 抽出后的新接口必须比原始调用点更清晰，否则不抽
- 每抽一个共享模块，至少让两个调用点接入，否则不要宣称其已稳定沉淀

## 7.9 UI 组件库实施方案

### 7.9.1 总体设计

建议把 UI 组件体系设计成：

- `EditorStyleCatalog`
  - 管理 spacing、padding、row height、icon gap、button accent、overlay colors
- `EditorWidgetLibrary`
  - 管理公共组件定义与命名
- `Panel`
  - 持有业务状态，调用组件 API 完成展示

推荐关系：

```text
Panel State + Domain Data
        ↓
Composite Component
        ↓
Primitive Widget
        ↓
UIContext / ImGui
```

### 7.9.2 第一阶段可直接落地的组件接口

建议第一版就把接口做成显式参数风格，例如：

```cpp
struct EditorToggleButtonDesc
{
    const char* label = nullptr;
    bool value = false;
};

struct EditorConfirmDialogDesc
{
    const char* popup_id = nullptr;
    const char* title = nullptr;
    const char* message = nullptr;
    const char* confirm_label = "Apply";
    const char* cancel_label = "Cancel";
    bool confirm_enabled = true;
};
```

然后由组件层返回结果，而不是直接在组件里改业务状态：

```cpp
struct EditorConfirmDialogResult
{
    bool confirmed = false;
    bool cancelled = false;
};
```

这种接口风格更适合后续复用，也更容易测试。

### 7.9.3 组件建设顺序

建议按下面顺序推进：

1. `EditorStyleCatalog`
   - 统一当前分散的 spacing / padding / accent color / row metrics
2. `EditorToggleButton`
   - 先替换 `ViewportPanel`、`InspectorPanel`、`AssetBrowserPanel` 的重复按钮样式
3. `EditorActionMenu`
   - 统一 shortcut 显示和 invoke 逻辑
4. `EditorConfirmDialog`
   - 先接 `Delete Entity`
5. `EditorFormDialogScaffold`
   - 再接 `Rename Entity`、`Reparent Entity`
6. `EditorEmptyState`
   - 接 service unavailable / surface unavailable / empty content

### 7.9.4 组件库管理规则

为了避免组件库失控，建议增加以下准入规则：

- 组件必须有清晰命名和职责说明
- 组件必须说明自己属于 primitive / composite / domain 哪一层
- 组件若依赖图标、action、theme token，必须显式声明
- 不允许把一整段 panel 业务流程直接包装成“组件”
- 组件进入公共库前，至少有一个清晰的第二使用场景

还应补充以下修改规则：

- 若组件已有多个调用方，新增功能前必须评估是否影响既有行为
- 优先通过稳定、可枚举的参数与状态扩展组件
- 若扩展会让组件职责失真或引入大量特判，必须停止扩展并新建组件
- 新建组件时优先复用下层 primitive，不强行复用已经不匹配的 composite 组件
- 公共组件的默认参数必须保持向后兼容

### 7.9.5 对“一个地方管理所有 UI 组件”的结论

这个提议本身是合理的，而且对当前编辑器阶段是必要的。

但正确做法不是：

- 新建一个什么都管的 `UIManager`

而是：

- 建立统一的 `editor/UI` 组件库
- 建立统一的 style token 真源
- 建立统一的组件命名、接口、owner 规则
- 让 panel 继续保留自己的业务状态和流程编排

这个边界守住了，方案就合理；边界守不住，就会从“抽共性”滑向“制造新的全局耦合”。

---

## 8. 目录结构建议

建议未来把 `project/src/editor` 逐步整理为：

```text
editor/
├── App/
│   ├── EditorApplication.*
│   ├── EditorBootstrap.*
│   └── EditorSession.*
├── Shared/
│   ├── Actions/
│   ├── DragDrop/
│   ├── Presentation/
│   └── Validation/
├── UI/
│   ├── Components/
│   ├── Composite/
│   ├── Styling/
│   └── Widgets/
├── Shell/
│   ├── EditorShell.*
│   ├── MainMenuController.*
│   ├── DockLayoutController.*
│   └── PanelManager.*
├── Core/
│   ├── EditorAction.*
│   ├── EditorCommand.*
│   ├── EditorSelection.*
│   ├── EditorFrameContext.*
│   └── PanelDeps/
├── Panels/
├── Services/
│   ├── SceneDocumentService.*
│   ├── SceneHierarchyService.*
│   ├── SceneSnapshotService.*
│   ├── SceneTemplateService.*
│   ├── SelectionService.*
│   ├── UndoRedoService.*
│   ├── ActionRegistry.*
│   ├── EditorViewportService.*
│   └── AssetDatabaseService.*
├── Widgets/        // 过渡期兼容目录，可逐步迁到 UI/Widgets
├── Styling/        // 过渡期兼容目录，可逐步迁到 UI/Styling
└── Utilities/
```

当前阶段不要求一次性搬目录，但新增类尽量按这个方向落位。

---

## 9. 分阶段改造路线

## 阶段 0：基线稳定化

### 目标

在不引入大结构变化前，先把当前主线的行为和边界稳定下来。

### 任务

- 清理明显死代码和重复 helper
- 盘点 panel 间重复的 UI 片段与共享 helper
- 统一树控件、图标、拖拽交互的使用方式
- 给 `EditorApplication`、`SceneService`、`UndoRedoService`、`ViewportService` 补文档注释
- 把关键行为写进文档和验收清单

### 验收标准

- Editor 可以稳定启动
- SceneHierarchy / Inspector / AssetBrowser / Viewport 正常工作
- 当前可撤销编辑链路没有回归

### 当前状态

这一阶段已部分完成，可视为“进行中偏后段”。

---

## 阶段 1：拆 `EditorApplication`

### 目标

让 `EditorApplication` 从“全能协调器”降级为“生命周期入口”。

### 拆分建议

- `EditorBootstrap`
  - 初始化 settings、scene、assets、icons、viewport
- `EditorShell`
  - 主菜单、workspace host、dockspace
- `PanelManager`
  - 注册、构建、attach、detach、遍历 panel
- `SceneWorkflowCoordinator`
  - 新建场景、加载、重载、默认选择、状态重置

### 迁移顺序

1. 先抽 `PanelManager`
2. 再抽 `DockLayoutController`
3. 再抽 `MainMenuController`
4. 最后抽 `SceneWorkflowCoordinator`

### 风险

- 生命周期顺序错误
- panel open state 与 viewport presentation 同步断链

### 验收标准

- `EditorApplication` 头文件显著瘦身
- 菜单、布局、面板、场景切换各自位于独立类
- 功能行为不变

---

## 阶段 2：拆 Scene 领域服务

### 目标

把当前 `SceneService` 切成更清晰的编辑器领域服务。

### 目标拆分

- `SceneDocumentService`
- `SceneHierarchyService`
- `SceneSnapshotService`
- `SceneTemplateService`

### 迁移建议

#### 2.1 先抽 `SceneTemplateService`

这是风险最低的一步，因为它只影响默认场景初始化。

#### 2.2 再抽 `SceneSnapshotService`

把当前 JSON 序列化、snapshot restore 大块逻辑挪出去。

#### 2.3 最后抽 `SceneHierarchyService`

把 create / rename / delete / reparent / insert_at / validate 统一收口。

### 验收标准

- `SceneService` 文件体量明显下降
- Undo/Redo 仍可工作
- SceneHierarchyPanel 和 Inspector 不再各自复制场景规则

---

## 阶段 3：升级 Action / Command 框架

### 目标

建立可供全编辑器复用的统一 action 模型。

### 任务

- 把 `CommandService` 升级为 `ActionRegistry`
- 为 action 增加 `can_execute`、`is_checked`、`is_visible`
- 菜单不再长期手写 enable 逻辑
- Toolbar / ContextMenu 开始复用 action
- 为后续快捷键系统补齐 `shortcut_text`、`scope`、`tooltip` 等元数据

### 后续可扩展目标

- Command Palette
- Shortcut Map
- 用户自定义快捷键

### 验收标准

- 主菜单至少迁移到新 action 描述
- 资产刷新、undo/redo、场景保存、布局重置都走统一 action
- 至少一组“依赖选中态”的 action 通过 `can_execute` 驱动 enable，而不是菜单手写复制判断

---

## 阶段 3B：补齐快捷键与事件基础设施

### 目标

在不引入完整输入系统大重写的前提下，先把“action 元数据 / 真实 shortcut 触发 / editor 内部状态通知”这两块基础设施补齐。

### 任务

- 让 action shortcut 文案与真实触发规则来自同一真源
- 新增 `EditorShortcutService` 或等价 owner，接管 `handle_global_shortcuts()` 的 binding 定义
- 明确 global shortcut 与 panel-scoped shortcut 的 owner
- 统一文本输入态下的快捷键抑制规则
- 引入最小可用的 `EditorEventBus`
- 先接入 `ActiveSceneChanged`、`SelectionChanged`、`UndoStackChanged`、`ViewportLayoutReset`
- 让 `AssetBrowser` 的 `Enter` / `Backspace` 成为 panel-scoped shortcut 迁移样板

### 实施顺序建议

1. 先给 action 补 `can_execute`，消除菜单 enable 与 shortcut enable 的逻辑分叉
2. 再迁移 `EditorApplication::handle_global_shortcuts()` 的 binding 表
3. 然后试点迁移 `AssetBrowserPanel` 局部快捷键
4. 最后补 `EditorEventBus` 的消费者迁移，先迁最少但高价值的 UI 刷新点

### 验收标准

- 不再长期保留“菜单展示一套 shortcut，代码硬编码另一套检测”的结构
- 新增一个快捷键时，不需要同时改多处分散逻辑
- 至少两个以上跨模块 UI 刷新点改为事件驱动而非直接互调
- `WantTextInput` 抑制语义与现状保持一致
- `Ctrl+Y / Ctrl+Shift+Z`、`Enter`、`Backspace` 等兼容行为通过专项回归

---

## 阶段 4：缩窄 Panel 依赖

### 目标

从 `EditorContext` 迁移到“专用 deps + frame context”。

### 任务

- 新增 `EditorFrameContext`
- 为每个 panel 定义 `Deps`
- `SceneHierarchyPanel` 先做试点
- `AssetBrowserPanel` 和 `InspectorPanel` 再迁移

### 迁移顺序建议

1. `SceneHierarchyPanel`
2. `AssetBrowserPanel`
3. `InspectorPanel`
4. `ViewportPanel`
5. `ConsolePanel`

### 验收标准

- `EditorContext` 不再持续膨胀
- 面板头文件能明确看出自己的依赖面

---

## 阶段 4B：抽共享功能与 UI 组件

### 目标

在不改变业务语义的前提下，把已经验证稳定的共性能力与 UI 片段收口成共享模块和组件库。

### 任务

- 新增 `EditorStyleCatalog`
- 新增 `EditorToggleButton`
- 新增 `EditorActionMenu`
- 新增 `EditorConfirmDialog`
- 抽 `EditorDragDropPayloads`
- 迁移至少两个 panel 到共享组件

### 验收标准

- 至少两个 panel 删除重复的按钮样式代码
- 至少两个 context menu 不再自己手写 shortcut 文案查找和 invoke 流程
- 至少一个 modal 改为共用 scaffold
- panel 业务状态仍然保留在 panel 内，没有被组件库反向吞掉
- 通用组件的新增参数没有改变旧调用方的默认行为
- 对于差异较大的需求，没有为勉强复用而污染已有通用组件

---

## 阶段 5：升级 Selection / Undo 模型

### 目标

为多选、资产编辑器、图编辑器提前打基础。

### 任务

- 扩展 `EditorSelection`
- 扩展 `EditorCommandSelection`
- 引入 `SelectionSnapshot`
- 统一 selection restore 流程

### 验收标准

- Undo/Redo 可恢复 entity 之外的选择
- 不破坏现有场景编辑链路

---

## 阶段 6：重构 Viewport 体系

### 目标

把 Viewport 的 UI 状态、持久化状态和 runtime render binding 分层。

### 任务

- 拆 `ViewportRecord`
- 把 persistence model 抽成独立结构
- 把 render binding 管理从 UI 逻辑中分离
- 为未来新增 auxiliary viewport / play mode 留接口

### 验收标准

- `EditorViewportService` 不再是混合大类
- 新增一个 viewport 类型时，不需要改动过多旧逻辑

---

## 10. 近期落地优先级

按收益 / 风险比，推荐优先顺序如下：

1. 拆 `EditorApplication`
2. 抽 `SceneSnapshotService`
3. 抽 `SceneHierarchyService`
4. 升级 `ActionRegistry`
5. 补齐快捷键与编辑器事件基础设施
6. 缩窄 `EditorContext`
7. 升级 Selection/Undo
8. 重构 Viewport 分层

原因：

- `EditorApplication` 是当前最明显的集中风险点
- `SceneSnapshotService` 拆分收益高且相对可控
- `SceneHierarchyService` 与你当前正在强化的 SceneHierarchy 功能高度相关

---

## 11. 实施策略

## 11.1 每阶段都要保持可运行

每一阶段改造结束后，都要满足：

- 能编译
- 能启动
- 基础面板能打开
- 主工作流可用

## 11.2 避免目录大搬家和逻辑大重写同时发生

推荐顺序：

- 先抽类
- 再改调用
- 最后再整理目录

不要在同一个提交里同时做：

- 目录重命名
- 大块业务迁移
- API 改签名

## 11.3 新类先接管旧逻辑，再逐步优化

例如先把 `SceneService` 的 snapshot 逻辑整体搬到 `SceneSnapshotService`，保持行为一致；确认稳定后，再做更优雅的内部抽象。

## 11.4 文档先行，代码跟进

每阶段正式开工前，应先把：

- 改造范围
- 涉及文件
- 风险点
- 验收项

回写到对应文档中。

## 11.5 阶段闸门

每一阶段在进入下一阶段前，必须经过以下闸门。

### 闸门 A：静态结构闸门

至少满足：

- 新增类职责明确
- 没有明显重复实现长期并存
- 旧入口若未删除，已明确标注为 forwarding / compatibility path
- 没有新增“为了先跑起来”的匿名全局状态

### 闸门 B：编译闸门

至少满足：

- `premake5 vs2022` 已重新生成工程
- `.\build_editor.bat Debug x64` 通过，或等价地完成 `MSBuild Editor Debug x64`
- 新增文件已正确接入 Premake / 工程生成链路
- 无新引入的编译错误或 link error
- 至少完成一次 editor smoke（命令行 smoke 或人工最小 smoke）

### 闸门 C：行为闸门

至少满足：

- 本文档第 3.1 节的冻结基线未被破坏
- 本阶段涉及模块完成对应回归检查
- 与本阶段无关的模块没有明显连带回归

若本阶段涉及 action / shortcut / 事件系统改造，还必须额外满足：

- 菜单显示的 shortcut 与真实触发规则已可证明来自同一真源
- `WantTextInput` 抑制规则已手工验证
- 未新增 panel 之间的直接互调来模拟事件传播
- 每个新增 editor event 都有明确发布 owner 记录

若本阶段涉及共享模块 / UI 组件抽取，还必须额外满足：

- 新公共组件至少有两个明确消费者，或已标注为战略基础设施
- 抽取后没有把 panel 业务状态偷偷迁入公共组件
- 公共组件没有直接依赖 `EditorContext` 大包
- 公共样式 token 已集中到同一真源，而不是又新增一套散落常量
- 修改已有公共组件时，默认行为对既有调用方保持兼容
- 若需求与现有组件差异过大，已选择新建组件而非强行打补丁

### 闸门 D：文档闸门

至少满足：

- 本文档已回写阶段状态
- 若改动影响专题文档，已同步更新
- 若保留了过渡层，已记录后续移除条件

任一闸门未通过，都不应进入下一阶段。

## 11.6 提交拆分规则

后续改造默认按以下颗粒度拆分提交。

### 类型 A：纯搬运 / 抽壳提交

特征：

- 基本不改行为
- 主要是挪代码、抽类、转发

要求：

- 不顺带改业务语义
- 尽量单模块

### 类型 B：调用点切换提交

特征：

- 调用方从旧类迁到新类
- 对外行为应保持一致

要求：

- 每次只迁一类调用点
- 切换后立即编译和回归

### 类型 C：旧入口删除提交

特征：

- 清理 forwarding wrapper
- 删除死代码

要求：

- 必须在全调用点迁移完成后进行
- 删除前后都要回扫引用

### 类型 D：行为增强提交

特征：

- 在新架构稳定后，才开始功能增强

要求：

- 与架构拆分类提交分开
- 单独记录验收项

## 11.7 过渡适配器约定

重构期间允许存在过渡适配器，但必须遵守以下约定：

- 名称要表达其过渡性质，例如 `LegacySceneSnapshotAdapter`
- 文件头注释写明：
  - 当前用途
  - 依赖谁
  - 计划何时移除
- 只能做转发 / 兼容，不再继续叠加新业务
- 不允许新增第二层适配器套娃

如果某个 adapter 在两个阶段后仍未删除，应视为架构债务，优先清理。

## 11.8 回退条件

出现以下任一情况，应停止继续扩散重构并优先回退到上一个稳定点：

- SceneHierarchy 拖拽 / reparent / insert_at 行为异常
- Inspector 的 Apply / Revert / Undo 链路异常
- 保存 / 重载场景出现路径、状态或默认选择回归
- Viewport surface 不显示或尺寸同步异常
- 布局恢复、面板 open state 或菜单 action 明显失效
- 无法快速定位是结构问题还是业务问题

这里的“回退”不一定指 git 回滚，也可以是：

- 暂停迁移更多调用点
- 恢复旧入口为主路
- 新类继续保留但不再接管主路径

## 11.9 不通过定义

满足以下任一情况，本阶段默认视为“不通过”：

- 只能编译，不能稳定运行
- 需要依赖人工修补配置才能正常启动
- 用户可见行为发生变化但文档未声明
- 旧逻辑已删除，但新逻辑尚未完全覆盖
- 需要“后面补 Undo/Redo”才能恢复已有能力
- 需要“后面补布局恢复”才能恢复已有能力

换句话说，架构重构不能以“先破再补”为常态。

---

## 12. 验收与回归策略

每阶段完成后至少检查以下内容。

### 12.1 启动与布局

- Editor 能正常启动
- Dockspace 正常
- 面板 open/close 正常
- Layout reset 正常

### 12.2 Scene 工作流

- 新建场景
- 重载场景
- 保存场景
- 默认选中实体

### 12.3 SceneHierarchy

- 选择
- 重命名
- 删除确认
- reparent
- insert_at
- 拖拽 before / after / into

### 12.4 Inspector

- 名称编辑
- Transform 编辑
- Camera / Light / Mesh 组件编辑
- Apply / Revert
- Undo / Redo

### 12.5 AssetBrowser

- 目录树
- 图标 / 列表切换
- 搜索
- 过滤
- 激活 / 右键菜单

### 12.6 Viewport

- Scene / Game 视口显示
- 尺寸同步
- toolbar / overlays / stats
- panel open state

## 12.7 架构重构专项回归矩阵

除了模块功能回归外，架构改造还必须补做下面这组专项检查。

### 12.7.1 生命周期专项

- 重复启动 editor 时初始化行为一致
- shutdown 不因 owner 调整而出现访问空指针
- panel attach / detach 顺序仍可解释
- icon service / viewport service / undo service 不因新 owner 改变而提前销毁

### 12.7.2 转发链专项

- forwarding wrapper 不改变返回值和失败路径
- 新旧入口同时存在时，结果一致
- 删除旧入口前，全工程无残留调用

### 12.7.3 持久化专项

- 重构前后的 `EditorSettings.json` 仍可读
- 重构前后的 `ViewportLayout.json` 仍可读
- 未显式设计迁移前，旧配置不应失效

### 12.7.4 命令链专项

- 新建实体可 undo/redo
- 删除实体可 undo/redo
- rename 可 undo/redo
- reparent / insert_at 可 undo/redo
- Inspector 修改可 undo/redo

### 12.7.5 选择态专项

- 执行命令后 Selection 与当前语义一致
- Undo 后 Selection 回到当前语义定义的位置
- Reload / New Scene 后 Selection 重置行为未变

### 12.7.6 快捷键专项

- `Ctrl+N`、`Ctrl+R`、`Ctrl+S`、`Ctrl+Shift+R` 触发行为未变
- `Ctrl+Z`、`Ctrl+Y`、`Ctrl+Shift+Z` 行为未变
- `Ctrl+Shift+A`、`Ctrl+Alt+A`、`F2`、`Ctrl+Shift+P`、`Delete` 的 enable 与触发语义未变
- `AssetBrowser` 内容区 `Enter` / `Backspace` 行为未变
- 文本输入框聚焦时，全局快捷键抑制语义未变

### 12.7.7 事件系统专项

- `SelectionChanged`、`ActiveSceneChanged`、`UndoStackChanged` 等事件的 owner 唯一且明确
- 同一语义没有同时走“局部 observer + panel 互调 + event bus”三套长期并存路径
- 事件消费者移除后不会留下悬挂订阅
- 事件回调不引入明显的递归发布或级联副作用

### 12.7.8 共享模块与 UI 组件专项

- `EditorToggleButton`、`EditorActionMenu`、`EditorConfirmDialog` 等公共组件至少有两个消费点
- 抽取后 panel 代码减少重复，但业务可读性没有下降
- 公共组件只处理展示与交互骨架，不直接写 scene / selection / undo
- style token 不再在多个 panel 中复制常量
- `EditorTreeWidget` 与新增组件的职责边界清晰，没有互相重叠
- 修改公共组件后，旧调用点的默认视觉与行为没有意外变化
- 对差异较大的新需求，优先检查是否应新建组件，而不是继续堆参数和分支

## 12.8 每次重构的最小手工 smoke 清单

后续每完成一个可感知阶段，至少手工检查一次：

1. 启动 Editor
2. 打开 `Scene Hierarchy`
3. 打开 `Inspector`
4. 打开 `Asset Browser`
5. 确认 `Scene` / `Game` viewport 有内容
6. 新建一个 root entity
7. 拖动一个 entity 到另一个 entity 前后/内部
8. Rename 一个 entity
9. Delete 一个 entity 后执行 Undo / Redo
10. 在 Inspector 改 Transform 后执行 Undo / Redo
11. 刷新 Asset Browser
12. Reset Layout
13. 验证 `Ctrl+S`、`Ctrl+Z`、`Ctrl+Y` / `Ctrl+Shift+Z`
14. 在 `AssetBrowser` 内容区验证 `Enter` / `Backspace`

如果这 14 项中任意一项失败，本次重构默认不算收口。

## 12.9 每阶段建议记录模板

每次阶段完成后，建议在文档或交付说明里至少回写：

- 阶段名
- 改造范围
- 新增类
- 仍保留的过渡层
- 已验证的 smoke 项
- 未验证风险
- 是否允许进入下一阶段

## 12.10 与功能增强并行时的额外约束

如果某一阶段既包含架构重构，又包含少量功能增强，则必须把验收拆成两部分：

- 架构等价回归
- 功能新增验证

不能把“新增功能通过”当成“重构没有回归”的替代证据。

---

## 13. 本文档对应的第一阶段执行建议

后续正式按本文档改造时，建议第一批直接落以下工作：

1. 新增 `PanelManager`
2. 新增 `DockLayoutController`
3. 从 `EditorApplication` 中迁出面板 attach/detach 和默认布局逻辑

这是风险较低、收益最高、且不会打断当前 SceneHierarchy / Inspector 功能增强的第一阶段。

### 13.1 第一阶段的强约束

第一阶段只允许做“壳层拆分”，不允许碰以下行为语义：

- Scene 新建 / 保存 / 重载语义
- Undo/Redo 行为
- Selection 重置语义
- Viewport sync 语义
- AssetBrowser 交互语义
- SceneHierarchy 拖拽规则

换句话说，第一阶段可以搬：

- panel attach / detach 归属
- 默认布局构建归属
- 菜单绘制归属
- shell 内部 owner 关系梳理

但不能顺带改：

- 菜单里 action 的 enable 语义
- panel title / id
- viewport panel open state 同步方式
- log_message 的投递结果

### 13.2 第一阶段的推荐切分

建议严格拆成下面几步，每一步都可单独验证。

#### Step 1：抽 `PanelManager`

只迁移：

- panel 创建
- attach
- detach
- 遍历更新
- 遍历绘制

不迁移：

- menu
- dock layout
- scene session 逻辑

#### Step 2：抽 `DockLayoutController`

只迁移：

- workspace host
- dockspace
- default layout
- layout reset
- viewport layout persistence

不迁移：

- panel 本体创建
- scene 切换
- action 注册

#### Step 3：抽 `MainMenuController`

只迁移：

- 主菜单绘制
- menu 到 action 的调用桥接

不迁移：

- action 真正执行逻辑
- 场景保存逻辑本身

### 13.3 第一阶段之后紧接的下一批建议

在第一阶段壳层拆分完成并通过闸门后，再进入紧接的第二批：

1. 新增 `SceneSnapshotService`
2. 将 `SceneService` 的 snapshot 相关实现迁出
3. 为后续 `SceneHierarchyService` 抽离准备 forwarding 入口

这样可以避免“阶段 1 说只拆壳，但执行时已经开始拆 scene 领域”的边界冲突。

### 13.4 第一阶段完成判定

只有在以下条件全部成立时，第一阶段才算完成：

- `EditorApplication` 明显瘦身
- `PanelManager` 已成为 panel 生命周期唯一 owner
- `DockLayoutController` 已成为 layout 唯一 owner
- 运行时 smoke 清单通过
- 没有新增长期保留的重复 panel 管理逻辑
- 没有新增新的全局状态或静态单例绕过原有 service

若上述条件不全，默认继续收口，不进入第二阶段。

---

## 14. 非目标说明

本轮改造暂不追求：

- 一次性支持多文档
- 一次性支持完整快捷键系统
- 一次性重构所有 UI 控件抽象
- 一次性发明一个管理所有 widget 实例和所有 UI 状态的 `UIManager`
- 为了追求复用而把明显不同的 UI 交互强行塞进同一个通用组件
- 一次性把 Editor 做成插件化框架

这些能力应在主干架构稳定后逐步推进，而不是本轮同时引爆。

---

## 15. 后续维护约定

- 以后凡是编辑器架构调整，优先更新本文档
- 如果某一阶段方案已完成，要在文档中标记完成状态
- 如果阶段策略发生变化，应说明原因，不要直接悄悄偏离
- 具体实施前，可基于本文档再拆更细的阶段任务单

---

## 16. 当前结论

当前编辑器架构**可以继续演进，不需要推倒重来**。

真正需要做的是：

- 控制集中化
- 明确层次边界
- 让命令、会话、面板、领域服务各司其职

如果按本文档推进，改造后的编辑器会更适合继续承载：

- 更复杂的 SceneHierarchy
- 更完整的 Inspector
- Asset 侧更多编辑能力
- 多 viewport / play mode
- 后续更多工具面板

这份文档作为后续重构主线方案，后面默认按此执行。
