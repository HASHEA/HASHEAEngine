# Editor File Responsibilities

> 面向 AI 和后续重构使用。
>
> 用途：改 Editor 代码前，先确认目标文件职责；不要把新逻辑塞进职责不匹配的文件。
>
> 范围：`project/src/editor/**` 源码、构建入口、Editor 专属 shader。排除 `project/src/editor/product/**` 运行产物。

## 使用规则

| 规则 | 要求 |
| --- | --- |
| 先定位 | 新功能先找到最贴近的 panel / service / app / shell 文件，不要默认塞进 `EditorApplicationImpl.cpp`。 |
| 只改本层 | panel 管 UI 和用户意图，service 管领域状态，app 管装配和生命周期，shell 管菜单 / dock / 状态栏。 |
| 文件变大 | 单文件继续增长时，优先按垂直功能流拆，不按 Renderer / Controller / Helper 横切硬拆。 |
| 头文件职责 | `.h` 只声明稳定合同和必要值类型；实现细节放 `.cpp`，能前置声明就不要 include 重头。 |
| Support 文件 | `*Support.*` 只能放无状态小工具；一旦持有流程状态或跨多个领域调度，就应回到具体 panel / service。 |

## 根目录

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Editor.h` | 声明 Editor 应用壳，继承 engine `Application` 并持有 `EditorApplication`。 | 只放生命周期入口和 PIMPL/成员声明，不放 editor 业务。 |
| `Editor.cpp` | 实现 engine 生命周期到 `EditorApplication` 的桥接。 | 只管 bootstrap / shutdown / update / gui 转发，不装配具体 panel。 |
| `EditorEntrypoint.cpp` | 创建 engine `Application`，读取 editor settings 并填充启动配置。 | 只处理进程入口前配置，不初始化 Editor 运行时服务。 |
| `EditorPCH.h` | Editor 预编译头，放低层稳定公共头。 | 不加入 engine/editor 高层业务头，不用 PCH 掩盖 include 缺失。 |
| `EditorPCH.cpp` | PCH 编译单元。 | 不写业务代码。 |
| `premake5.lua` | Editor 工程生成规则。 | 新增源码目录或 shader 时同步维护。 |
| `Editor.vcxproj` | VS 生成后的 Editor 工程文件。 | 通常由 premake 生成，非必要不手改。 |
| `Editor.vcxproj.filters` | VS 解决方案筛选器。 | 通常由 premake 生成，非必要不手改。 |
| `Editor.vcxproj.user` | 本地 VS 用户配置。 | 不作为长期源码规则来源。 |

## App

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `App/EditorApplication.h` | 声明公开 Editor 应用薄壳。 | 保持 PIMPL，避免暴露服务、panel、controller 头。 |
| `App/EditorApplication.cpp` | 转发公开生命周期、GUI、viewport 查询到 Impl。 | 只做薄转发和空指针保护。 |
| `App/EditorApplicationImpl.h` | 声明 composition root 的成员和生命周期接口。 | 使用前置声明和 `unique_ptr`；不要把业务算法写进头。 |
| `App/EditorApplicationImpl.cpp` | 创建、注入、更新和关闭 Editor 主要服务 / panel / shell。 | 只做装配和主循环编排；panel 专有行为继续下沉。 |
| `App/EditorActionRegistrar.h` | 声明 command/action 注册入口。 | 只表达注册所需依赖，不持有运行状态。 |
| `App/EditorActionRegistrar.cpp` | 注册全局 Editor actions 和快捷键元数据。 | 只登记 action，不实现具体业务流程。 |
| `App/EditorActionCoordinator.h` | 声明 action 执行协调器及其依赖上下文。 | 只保留跨模块 action 的窄入口；避免继续扩大 context。 |
| `App/EditorActionCoordinator.cpp` | 执行 scene、viewport、panel open state 等跨模块 action。 | panel 内部 action 优先下沉到 panel 或窄 target。 |
| `App/PanelBootstrapper.h` | 声明 panel 创建和依赖注入入口。 | 只用于装配阶段，不向 update/draw 路径扩散。 |
| `App/PanelBootstrapper.cpp` | 创建主要 panels，绑定 `PanelDeps` 和 action target。 | 不写 panel 绘制和业务逻辑。 |
| `App/SceneWorkflowCoordinator.h` | 声明 scene 新建、打开、保存、重载流程协调器。 | 只暴露文档操作流程接口，不暴露 panel 细节。 |
| `App/SceneWorkflowCoordinator.cpp` | 编排 scene 文件操作、dirty state、通知、选择和 undo 清理。 | 文件 IO / 状态变更要有日志和事件。 |
| `App/ViewportLayoutPersistence.h` | 声明 viewport layout 持久化桥。 | 只依赖 settings 与 viewport service。 |
| `App/ViewportLayoutPersistence.cpp` | 加载、保存和重置 viewport layout。 | 不处理 viewport 绘制或相机输入。 |
| `App/ViewportPanelStateBridge.h` | 声明 viewport panel open state 与 viewport service 的同步桥。 | 只做状态桥接，不做 UI。 |
| `App/ViewportPanelStateBridge.cpp` | 监听 panel open state，更新 viewport 服务和布局。 | 不直接访问具体 viewport panel 实现细节。 |
| `App/EditorLogBridge.h` | 声明 spdlog → `EditorEventBus` 的日志桥，跨线程暂存 `EditorLogEvent`。 | 只做日志转发，不做过滤与展示（那是 ConsolePanel）。 |
| `App/EditorLogBridge.cpp` | 实现 spdlog sink 挂接、待发队列与 `FlushPending` 派发。 | 事件只在主线程 flush 时 Publish，不在 sink 线程直接派发。 |

## Core

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Core/EditorPanel.h` | 定义所有 panel 的基类、名称和 open state 合同。 | 保持轻量，不加入具体服务依赖。 |
| `Core/EditorPanel.cpp` | 实现 panel 基础 open state 行为。 | 不放任何具体 panel 逻辑。 |
| `Core/EditorContext.h` | 旧式共享上下文桥接。 | 只作为兼容过渡，新增代码不要继续依赖整包 context。 |
| `Core/EditorFrameContext.h` | 定义单帧 draw/update 所需的窄上下文。 | 只放每帧通用输入，不塞 service 聚合包。 |
| `Core/EditorFrameContext.cpp` | 从旧 context 构造 frame context 的兼容实现。 | 后续逐步改为直接从明确依赖构造。 |
| `Core/EditorCommand.h` | 定义可撤销命令、选择恢复模式和复合命令。 | 命令只表达编辑状态变更，不做 UI。 |
| `Core/EditorCommand.cpp` | 实现命令基础辅助逻辑。 | 不依赖具体 panel。 |
| `Core/EntityCommands.h` | 声明实体创建、删除、重命名、变换、组件修改等命令。 | 新增 scene 编辑能力优先命令化。 |
| `Core/EntityCommands.cpp` | 实现实体相关命令的 do/undo。 | 必须维护选择恢复、dirty state 和资产引用一致性。 |
| `Core/EditorSelection.h` | 定义 editor 选择目标类型和值。 | 只放选择数据类型，不放选择服务逻辑。 |
| `Core/EditorIds.h` | 定义 Editor 稳定 ID / 常量。 | 只放跨模块共享且稳定的 ID。 |
| `Core/EditorGizmoTypes.h` | 定义公开 gizmo 模式、空间、pivot、snap 和状态。 | 只放 public UI state，不放内部命中 / 渲染结构。 |
| `Core/EditorViewportTypes.h` | 定义 viewport state、实例和显示状态。 | 只放跨 panel/service 共享的 viewport 类型。 |
| `Core/EditorSceneTypes.h` | 定义 scene 文件 / scene 状态相关共享类型。 | 不放 scene service 实现。 |
| `Core/EditorEventTypes.h` | 定义 event bus 底层事件 ID 类型。 | 保持与事件内容解耦。 |
| `Core/EditorEvents.h` | 定义 Editor 事件 payload。 | 新事件必须是数据载体，不包含处理逻辑。 |
| `Core/EditorEventBus.h` | 声明轻量事件总线。 | 避免继续塞具体事件实现或业务依赖。 |
| `Core/EditorEventBus.cpp` | 实现事件总线基础生命周期。 | 不订阅具体业务事件。 |
| `Core/EditorEventBindings.h` | 声明事件订阅 RAII 绑定集合。 | 用于 owner 管理订阅生命周期。 |
| `Core/EditorEventBindings.cpp` | 实现绑定释放和移动语义。 | 不包含事件业务处理。 |
| `Core/AssetPresentationUtils.h` | 声明资产名、类型、图标、显示文本等展示辅助。 | 只做展示转换，不查数据库状态之外的业务。 |
| `Core/AssetPresentationUtils.cpp` | 实现资产展示辅助函数。 | 可被 panel 复用，但不要持有 UI 状态。 |
| `Core/EditorComponentComparison.h` | 声明组件值比较辅助。 | 只判断编辑前后是否变化。 |
| `Core/EditorComponentComparison.cpp` | 实现 transform/camera/light/mesh 等组件比较。 | 不执行命令、不写 scene。 |
| `Core/EditorScenePathUtils.h` | 声明 scene 路径解析、默认路径、显示路径工具。 | 只处理路径策略，不做 scene 加载。 |
| `Core/EditorScenePathUtils.cpp` | 实现 scene 路径工具。 | 文件系统失败要返回可处理结果，不直接吞错误。 |
| `Core/EditorPathUtils.h` | 声明通用文件系统路径工具：祖先判断、排序去重、剔除嵌套子路径。 | 只放纯路径计算，不做 IO。 |
| `Core/EditorPathUtils.cpp` | 实现通用路径工具。 | 不引入 UI 或 scene 依赖。 |
| `Core/EditorViewportInputState.h` | 定义 viewport 每帧输入快照（鼠标/按键/修饰键状态与查询）。 | 只放输入数据快照，不做输入解释。 |
| `Core/EditorStringUtils.h` | 声明 Editor 字符串小工具。 | 只放通用字符串处理。 |
| `Core/EditorStringUtils.cpp` | 实现字符串 trim、case、匹配等工具。 | 不引入 UI 或 scene 依赖。 |
| `Core/PlatformFileDialog.h` | 声明平台文件选择对话框接口和选项。 | 只暴露 Editor 需要的最小合同。 |
| `Core/PlatformFileDialog.cpp` | 实现平台文件打开 / 保存对话框。 | 平台分支要局部化，不外溢到 panel。 |
| `Core/SceneSnapshotTypes.h` | 定义实体和组件快照类型。 | 只放命令/复制粘贴需要的数据结构。 |
| `Core/SceneSnapshotUtils.h` | 声明 scene/entity 快照采集和恢复工具。 | 只处理快照转换，不画 UI。 |
| `Core/SceneSnapshotUtils.cpp` | 实现快照采集、恢复、复制粘贴辅助。 | 修改 scene 时注意父子层级和 ID 映射。 |
| `Core/SceneSnapshotComponentUtils.h` | 声明组件级快照采集与回放工具。 | 只处理组件快照转换，不画 UI。 |
| `Core/SceneSnapshotComponentUtils.cpp` | 实现组件快照 capture/apply（含移除快照中缺失的可选组件）。 | 对 scene 的写入只应由命令流程发起。 |
| `Core/SceneComponentSerialization.h` | 声明 scene 组件序列化辅助。 | 只服务 scene 文件与快照序列化。 |
| `Core/SceneComponentSerialization.cpp` | 实现组件到 JSON/数据结构的序列化细节。 | 不处理文件路径和 UI。 |
| `Core/IActionInvoker.h` | 定义 action 调用窄接口。 | 只用于解耦 UI 与 command service。 |
| `Core/IAssetBrowserActionTarget.h` | 定义 AssetBrowser action target。 | 只暴露 asset browser 外部需要触发的动作。 |
| `Core/IEditorActionHandler.h` | 定义 command service 调用 action 的处理接口。 | 不放具体 action 枚举之外的业务。 |
| `Core/IEditorCommandExecutor.h` | 定义执行 editor command 的窄接口。 | panel 通过它提交命令，不直接碰 undo 栈。 |
| `Core/IEditorViewportBindingResolver.h` | 定义 viewport scene/camera 绑定解析接口。 | 相机服务实现它，panel 只依赖接口。 |
| `Core/INotificationSink.h` | 定义通知输出窄接口。 | 用于 service/coordinator 发通知，不依赖 ConsolePanel。 |
| `Core/ISceneFileActionHandler.h` | 定义 scene 文件 action 窄接口。 | shell/menu 通过它触发文件操作。 |
| `Core/ISceneHierarchyActionTarget.h` | 定义 SceneHierarchy action target。 | 只暴露 hierarchy 外部动作，不暴露内部状态。 |
| `Core/IThemeApplier.h` | 定义主题应用接口。 | 只用于 shell/action 与 theme 实现解耦。 |

## Core/PanelDeps

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Core/PanelDeps/AssetBrowserPanelDeps.h` | 定义 AssetBrowserPanel 依赖包。 | 只放该 panel 直接需要的服务指针/引用。 |
| `Core/PanelDeps/AssetPreviewPanelDeps.h` | 定义 AssetPreviewPanel 依赖包。 | 避免带入完整 editor context。 |
| `Core/PanelDeps/ConsolePanelDeps.h` | 定义 ConsolePanel 依赖包。 | 只放设置/日志展示需要的依赖。 |
| `Core/PanelDeps/InspectorPanelDeps.h` | 定义 InspectorPanel 依赖包。 | 只放 inspector 编辑链路需要的 service/command 接口。 |
| `Core/PanelDeps/SceneHierarchyPanelDeps.h` | 定义 SceneHierarchyPanel 依赖包。 | hierarchy 只拿 scene/selection/command/drag/drop/icon 等必需依赖。 |
| `Core/PanelDeps/ViewportPanelDeps.h` | 定义 ViewportPanel 依赖包。 | viewport 依赖必须明确，不回退到 `EditorContext`。 |

## ImGui

> 本目录已被 `project/src/editor/premake5.lua` 的 `removefiles` 剔出构建（运行时 Editor UI 全部走引擎侧 `UIContext`），仅作历史参考，恢复启用前需先补齐 `UIContext` 等价扩展点。

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `ImGui/EditorImGuiLayer.h` | 声明遗留的编辑器侧 ImGui layer。 | 不参与构建；不要在活跃路径重新引用。 |
| `ImGui/EditorImGuiLayer.cpp` | 实现遗留 ImGui layer 生命周期。 | 同上。 |
| `ImGui/EditorStyle.h` | 声明遗留的 Editor 主题应用。 | 同上；活跃主题走 `Widgets/EditorThemeColors` + settings。 |
| `ImGui/EditorStyle.cpp` | 实现遗留 UI 主题颜色和样式。 | 同上。 |

## Panels

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Panels/AssetBrowserPanel.h` | 声明资产浏览主面板和对外 action target。 | 保持协调者角色，不继续堆具体子视图绘制。 |
| `Panels/AssetBrowserPanel.cpp` | 协调资产浏览状态、子视图、拖拽、预览和 action。 | 目录树、内容区、菜单细节继续放子文件。 |
| `Panels/AssetPreviewPanel.h` | 声明资产预览面板。 | 只暴露预览 panel 生命周期。 |
| `Panels/AssetPreviewPanel.cpp` | 绘制资产预览 UI 并调用预览服务。 | 不实现 asset database 扫描逻辑。 |
| `Panels/ConsolePanel.h` | 声明日志/通知面板、消息类型和通知接口实现。 | 对外只作为 notification sink，不暴露内部过滤状态。 |
| `Panels/ConsolePanel.cpp` | 收集、过滤、绘制 Editor console 消息。 | 日志来源不要反向依赖 console。 |
| `Panels/InspectorPanel.h` | 声明 Inspector 主面板和组件编辑 host。 | 保持选择对象协调者，不塞所有组件 editor 细节。 |
| `Panels/InspectorPanel.cpp` | 协调 Inspector 当前选择、组件编辑器和命令提交。 | 具体组件 UI 优先放 `Panels/Inspector/*Editor.*`。 |
| `Panels/SceneHierarchyPanel.h` | 声明 SceneHierarchy 主面板和 action target。 | 只保留 hierarchy 状态协调与外部动作入口。 |
| `Panels/SceneHierarchyPanel.cpp` | 协调 hierarchy toolbar/tree/search/modal/drag/drop。 | 具体子窗口和 modal 继续放子文件。 |
| `Panels/ViewportPanel.h` | 声明 viewport 主面板。 | 只负责 panel 外壳和子模块协调。 |
| `Panels/ViewportPanel.cpp` | 协调 toolbar、canvas、interaction、viewport service 同步。 | 绘制、输入、overlay 细节继续下沉。 |
| `Panels/ViewportPanelToolbar.h` | 声明 viewport toolbar 绘制入口。 | 只处理 toolbar UI，不处理画布输入。 |
| `Panels/ViewportPanelToolbar.cpp` | 绘制 viewport 模式、相机、gizmo、显示选项工具栏。 | toolbar action 要通过明确服务/状态。 |
| `Panels/ViewportPanelCanvas.h` | 声明 viewport 画布绘制结果。 | 只表达画布区域、hover/focus、尺寸等结果。 |
| `Panels/ViewportPanelCanvas.cpp` | 绘制 viewport texture/canvas，返回交互区域信息。 | 不处理相机控制和 gizmo 编辑。 |
| `Panels/ViewportPanelInteraction.h` | 声明 viewport 输入/框选状态和交互入口。 | 只处理输入解释，不做渲染。 |
| `Panels/ViewportPanelInteraction.cpp` | 处理 viewport 点击、框选、拖拽、相机/gizmo 输入分发。 | 复杂交互拆垂直子流程，不塞回 panel 主文件。 |
| `Panels/ViewportPanelInteractionSupport.cpp` | 放 viewport interaction 的局部辅助函数。 | 若逻辑开始持有状态，应并回具体 interaction 类。 |
| `Panels/ViewportPanelOverlaySupport.cpp` | 放 viewport overlay 绘制辅助。 | 只处理 overlay 小绘制，不碰 scene 状态写入。 |
| `Panels/ViewportPanelSceneSupportInternal.h` | 声明 viewport scene 投影等内部辅助结构。 | 仅供 viewport support 内部使用，能移进 cpp 就移。 |
| `Panels/ViewportPanelSceneSupportInternal.cpp` | 实现 viewport scene/project/unproject 辅助。 | 不成为跨模块公共 API。 |
| `Panels/ViewportPanelState.h` | 定义 viewport 框选与待决 GPU pick 等交互状态。 | 只放状态数据，交互逻辑在 `ViewportPanelInteraction`。 |
| `Panels/ViewportPanelSupport.h` | 声明 viewport 支撑工具。 | 防止变成 God Helper；只放无状态公共函数。 |
| `Panels/ViewportPanelSupport.cpp` | 实现 viewport 支撑工具。 | 新增流程逻辑优先放 canvas/interaction/service。 |

## Panels/AssetBrowser

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Panels/AssetBrowser/AssetBrowserPanelState.h` | 定义 AssetBrowser UI 状态。 | 只放状态数据，不放绘制和业务。 |
| `Panels/AssetBrowser/IAssetBrowserViewHost.h` | 定义 AssetBrowser 子视图回调 host。 | 后续应继续收窄，避免成为内部 service locator。 |
| `Panels/AssetBrowser/AssetBrowserSupport.h` | 声明资产浏览过滤、目录树、frame data 辅助类型和函数。 | 只放可复用纯辅助，不持有 panel 状态。 |
| `Panels/AssetBrowser/AssetBrowserSupport.cpp` | 实现资产过滤、目录构建、可见项计算等辅助。 | 不绘制 UI。 |
| `Panels/AssetBrowser/AssetBrowserToolbarView.h` | 声明资产浏览工具栏视图。 | 只处理搜索、过滤、视图模式等头部 UI。 |
| `Panels/AssetBrowser/AssetBrowserToolbarView.cpp` | 绘制资产浏览工具栏。 | 可考虑吸收 breadcrumb，减少过细拆分。 |
| `Panels/AssetBrowser/AssetBrowserBreadcrumbsView.h` | 声明路径面包屑视图。 | 职责很窄，后续可与 toolbar 合并。 |
| `Panels/AssetBrowser/AssetBrowserBreadcrumbsView.cpp` | 绘制当前路径面包屑和跳转。 | 不处理目录扫描。 |
| `Panels/AssetBrowser/AssetBrowserDirectoryTreeView.h` | 声明目录树视图。 | 只处理目录树 UI 和选择结果。 |
| `Panels/AssetBrowser/AssetBrowserDirectoryTreeView.cpp` | 绘制资产目录树。 | 不处理资产内容区列表。 |
| `Panels/AssetBrowser/AssetBrowserContentView.h` | 声明资产内容区视图和 draw result。 | 只处理当前目录内容展示。 |
| `Panels/AssetBrowser/AssetBrowserContentView.cpp` | 绘制资产网格/列表、选择、双击、拖拽入口。 | 不做数据库扫描和全局 action 注册。 |
| `Panels/AssetBrowser/AssetBrowserContextMenus.h` | 声明 AssetBrowser 右键菜单视图。 | 只暴露菜单绘制入口。 |
| `Panels/AssetBrowser/AssetBrowserContextMenus.cpp` | 绘制目录/资产/空白区域上下文菜单。 | 菜单动作通过 host/target 触发，不直接改全局状态。 |

## Panels/Inspector

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Panels/Inspector/InspectorPanelState.h` | 定义 Inspector 草稿状态。 | 只放 UI draft，不放命令提交逻辑。 |
| `Panels/Inspector/IInspectorComponentHost.h` | 定义组件编辑器回调 host。 | 子编辑器通过 host 提交命令，不直接碰主 panel 私有状态。 |
| `Panels/Inspector/InspectorComponentEditor.h` | 定义组件编辑器基类。 | 保持组件 editor 的统一 draw 合同。 |
| `Panels/Inspector/InspectorComponentEditorSupport.h` | 声明组件编辑器通用辅助。 | 只放字段绘制/资产选择等共享小逻辑。 |
| `Panels/Inspector/InspectorComponentEditorSupport.cpp` | 实现组件编辑器共享辅助。 | 不持有跨帧状态。 |
| `Panels/Inspector/CameraComponentEditor.h` | 声明 Camera 组件编辑器。 | 只处理 CameraComponent UI。 |
| `Panels/Inspector/CameraComponentEditor.cpp` | 绘制 camera 字段并提交 camera 修改命令。 | 不处理其他组件。 |
| `Panels/Inspector/LightComponentEditor.h` | 声明 Light 组件编辑器。 | 只处理 LightComponent UI。 |
| `Panels/Inspector/LightComponentEditor.cpp` | 绘制 light 字段并提交 light 修改命令。 | 不处理其他组件。 |
| `Panels/Inspector/MeshComponentEditor.h` | 声明 Mesh 组件编辑器。 | 只处理 MeshComponent UI。 |
| `Panels/Inspector/MeshComponentEditor.cpp` | 绘制 mesh 字段、资产选择和 mesh 修改命令。 | 资产预览/数据库逻辑不写在这里。 |
| `Panels/Inspector/InspectorPanelDrafts.cpp` | 实现 Inspector draft 同步和 dirty 判断。 | 只处理草稿状态，不绘制完整 UI。 |
| `Panels/Inspector/InspectorPanelSections.cpp` | 实现 Inspector 各 section 绘制。 | 只按 section 组织 UI，不做 service 装配。 |
| `Panels/Inspector/InspectorPanelSupport.h` | 声明 Inspector 组件显示/编辑辅助。 | 防止变成主逻辑垃圾桶。 |
| `Panels/Inspector/InspectorPanelSupport.cpp` | 实现 Inspector 辅助函数。 | 复杂组件逻辑应放独立 component editor。 |

## Panels/SceneHierarchy

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Panels/SceneHierarchy/SceneHierarchyPanelState.h` | 定义 SceneHierarchy 搜索、rename、reparent、delete 等 UI 状态。 | 只放状态；后续可按子流程继续下沉。 |
| `Panels/SceneHierarchy/SceneHierarchyPanelSupport.h` | 声明 hierarchy 过滤、创建 preset、拖拽等辅助。 | 只放无状态 helper，避免成为 God Helper。 |
| `Panels/SceneHierarchy/SceneHierarchyPanelSupport.cpp` | 实现 hierarchy 辅助逻辑。 | 状态机和 modal 流程不要继续塞这里。 |
| `Panels/SceneHierarchy/SceneHierarchyToolbarView.h` | 声明 hierarchy toolbar。 | 只处理搜索、创建按钮、过滤入口。 |
| `Panels/SceneHierarchy/SceneHierarchyToolbarView.cpp` | 绘制 hierarchy toolbar。 | 不处理树节点递归。 |
| `Panels/SceneHierarchy/SceneHierarchyTreeView.h` | 声明 scene entity tree 视图。 | 只处理实体树展示和选择/拖拽结果。 |
| `Panels/SceneHierarchy/SceneHierarchyTreeView.cpp` | 绘制 scene entity 树、展开状态、拖拽目标。 | 不提交最终命令，交给主 panel 协调。 |
| `Panels/SceneHierarchy/SceneHierarchySearchResultsView.h` | 声明搜索结果视图。 | 只处理搜索结果列表。 |
| `Panels/SceneHierarchy/SceneHierarchySearchResultsView.cpp` | 绘制搜索结果并返回选择意图。 | 不修改 scene。 |
| `Panels/SceneHierarchy/SceneHierarchyRenameModal.h` | 声明重命名 modal。 | 只处理 rename UI 状态和确认结果。 |
| `Panels/SceneHierarchy/SceneHierarchyRenameModal.cpp` | 绘制 rename modal。 | 命令提交由主 panel 或 action target 执行。 |
| `Panels/SceneHierarchy/SceneHierarchyReparentModal.h` | 声明改父级 modal。 | 只处理 reparent UI 状态和确认结果。 |
| `Panels/SceneHierarchy/SceneHierarchyReparentModal.cpp` | 绘制 reparent modal。 | 不直接执行 reparent 命令。 |
| `Panels/SceneHierarchy/SceneHierarchyDeleteModal.h` | 声明删除确认 modal。 | 只处理 delete confirmation。 |
| `Panels/SceneHierarchy/SceneHierarchyDeleteModal.cpp` | 绘制删除确认 modal。 | 不直接改 scene。 |

## Services

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Services/SceneService.h` | 声明当前 scene 生命周期、文件状态和 dirty state 服务。 | Scene 状态统一从这里进出。 |
| `Services/SceneService.cpp` | 实现 scene 新建、打开、保存、重载、dirty 管理。 | 文件操作必须日志清楚，失败可诊断。 |
| `Services/SelectionService.h` | 声明选择状态服务。 | 选择修改统一走这里。 |
| `Services/SelectionService.cpp` | 实现选择设置、合并、清空和选择事件。 | 不耦合具体 panel。 |
| `Services/UndoRedoService.h` | 声明 undo/redo 栈、事务和命令执行服务。 | 头文件保持轻依赖，命令实现放 cpp。 |
| `Services/UndoRedoService.cpp` | 实现命令执行、撤销、重做、事务和事件。 | 命令失败/事务异常要有日志或通知。 |
| `Services/CommandService.h` | 声明 action 注册、快捷键绑定和 action dispatch。 | 不实现具体业务动作。 |
| `Services/CommandService.cpp` | 实现 action 查找、执行、启用状态和事件发布。 | action handler 由外部注入。 |
| `Services/EditorShortcutService.h` | 声明快捷键输入解析服务。 | 只处理输入到 action 的映射。 |
| `Services/EditorShortcutService.cpp` | 实现 shortcut scope、冲突处理和 action 触发。 | 不直接执行业务，走 command service。 |
| `Services/EditorSettingsService.h` | 声明 Editor 配置结构、加载、保存和路径解析。 | 配置字段变更要兼容旧配置。 |
| `Services/EditorSettingsService.cpp` | 实现 settings JSON IO、workspace root 发现和默认值。 | 失败要日志；不要依赖 panel。 |
| `Services/EditorSessionStateService.h` | 声明状态栏/session 聚合服务。 | 只汇总运行状态，不做业务变更。 |
| `Services/EditorSessionStateService.cpp` | 实现 session 状态刷新和事件监听。 | 不成为全局 service locator。 |
| `Services/AssetDatabaseService.h` | 声明资产数据库扫描和查询服务。 | asset 信息统一从这里读取。 |
| `Services/AssetDatabaseService.cpp` | 实现资产扫描、缓存、查找、刷新。 | IO 和解析失败要可诊断。 |
| `Services/AssetPreviewService.h` | 声明资产预览状态服务。 | 只保存当前预览目标和轻量信息。 |
| `Services/AssetPreviewService.cpp` | 实现预览目标设置和查询。 | 不做重型资产加载。 |
| `Services/DragDropTransferService.h` | 声明 Editor 内部拖拽数据服务。 | 只承载拖拽 payload，不做业务落地。 |
| `Services/DragDropTransferService.cpp` | 实现拖拽数据设置、读取、清理。 | drop 后业务由目标 panel/service 处理。 |
| `Services/EditorViewportService.h` | 声明 editor viewport 实例、presentation 和持久化状态服务。 | viewport 状态统一从这里同步。 |
| `Services/EditorViewportService.cpp` | 实现 viewport 注册、主 viewport、render target 和 presentation 更新。 | 不处理 UI 绘制和相机输入。 |
| `Services/EditorViewportCameraService.h` | 声明 viewport 相机输入、raycast、绑定解析服务。 | 只处理相机和 picking 相关领域。 |
| `Services/EditorViewportCameraService.cpp` | 实现 viewport 相机控制、scene ray、hover/selection 查询。 | 不绘制 gizmo，不提交编辑命令。 |
| `Services/EditorGizmoService.h` | 声明 gizmo 总协调服务。 | 只协调 gizmo 状态、工具和 overlay，不塞具体工具算法。 |
| `Services/EditorGizmoService.cpp` | 实现 gizmo 工具分发、状态同步和对外交互入口。 | move/scale/rotate 细节留在工具类。 |
| `Services/EditorGizmoServiceTypes.h` | 定义 gizmo viewport context 和 interaction result。 | 只放 service 公开 API 需要的轻量类型。 |
| `Services/EditorGizmoTypesInternal.h` | 定义 gizmo 内部视觉、命中、拖拽类型。 | 只供 gizmo 内部使用，不给 panel 依赖。 |
| `Services/MoveScaleGizmoTool.h` | 声明移动/缩放 gizmo 工具。 | 只处理 move/scale 模式。 |
| `Services/MoveScaleGizmoTool.cpp` | 实现移动/缩放手柄生成、命中、拖拽和命令提交。 | 不混入 rotate 逻辑。 |
| `Services/RotateGizmoTool.h` | 声明旋转 gizmo 工具。 | 只处理 rotate 模式。 |
| `Services/RotateGizmoTool.cpp` | 实现旋转环生成、命中、拖拽和命令提交。 | 不混入 move/scale 逻辑。 |
| `Services/SelectionOverlayRenderer.h` | 声明选中对象 overlay 渲染器。 | 只做选择可视化，不修改 scene。 |
| `Services/SelectionOverlayRenderer.cpp` | 实现 selection bounds、outline、标签等 overlay。 | 数据读取失败要安全跳过。 |
| `Services/EditorGizmoMath.h` | 声明 gizmo 数学工具。 | 只放 gizmo 复用数学。 |
| `Services/EditorGizmoMath.cpp` | 实现 ray/plane/axis/project 等数学辅助。 | 不依赖 UI 状态。 |
| `Services/EditorGizmoViewport.h` | 声明 gizmo viewport 坐标转换工具。 | 只处理 viewport/projection 相关转换。 |
| `Services/EditorGizmoViewport.cpp` | 实现 gizmo screen/world 坐标转换。 | 不提交命令。 |
| `Services/EditorGizmoTransform.h` | 声明 gizmo transform 读取/写入辅助。 | 只处理选择实体 transform 聚合。 |
| `Services/EditorGizmoTransform.cpp` | 实现 transform pivot、应用、快照辅助。 | 修改 scene 必须通过命令或明确执行器。 |
| `Services/EditorGizmoSelectionUtils.h` | 声明 gizmo 选择相关工具。 | 只处理选择集合到实体/边界的转换。 |
| `Services/EditorGizmoSelectionUtils.cpp` | 实现 gizmo 选择过滤和 bounds 查询。 | 不处理 UI 输入。 |
| `Services/EditorGizmoStyle.h` | 声明 gizmo 颜色/样式辅助。 | 只保留多工具共享样式；单工具私有样式应下沉。 |
| `Services/EditorGizmoStyle.cpp` | 实现 gizmo 样式和颜色计算。 | 防止变成工具逻辑集合。 |
| `Services/EditorSceneBoundsUtils.h` | 声明 scene/editor bounds 查询工具。 | 只处理 bounds 计算。 |
| `Services/EditorSceneBoundsUtils.cpp` | 实现 entity/selection/asset bounds 辅助。 | 不绘制、不提交命令。 |
| `Services/EditorIconService.h` | 声明 icon texture 加载和绘制服务。 | 对外依赖 `IEditorIconService`。 |
| `Services/EditorIconService.cpp` | 实现 icon 资源加载、缓存、绘制。 | 资源失败要日志清楚。 |
| `Services/IEditorIconService.h` | 定义 icon service 窄接口和 icon ID。 | panel 只依赖此接口，不依赖具体实现。 |

## Shell

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Shell/PanelManager.h` | 声明 panel 注册、查找、open state 和 draw 调度。 | 不包含具体 panel 头，使用基类和窄接口。 |
| `Shell/PanelManager.cpp` | 实现 panel 生命周期、绘制顺序、open state 事件。 | 不写具体 panel 内部 UI。 |
| `Shell/MainMenuController.h` | 声明主菜单控制器。 | 只暴露菜单 draw 入口和上下文。 |
| `Shell/MainMenuController.cpp` | 绘制菜单栏并触发 command/action。 | 菜单项不直接改业务状态，走 action/service。 |
| `Shell/DockLayoutController.h` | 声明 dockspace/layout 控制器。 | 只处理 workspace 布局。 |
| `Shell/DockLayoutController.cpp` | 实现 dockspace 绘制、默认布局、layout 重置。 | 不处理 panel 内容绘制。 |
| `Shell/EditorCommandPaletteController.h` | 声明命令面板控制器。 | 只处理 command palette UI。 |
| `Shell/EditorCommandPaletteController.cpp` | 绘制命令搜索、过滤、执行 UI。 | action 执行走 command service。 |
| `Shell/EditorStatusBarController.h` | 声明状态栏控制器。 | 只读 session state 和 UI context。 |
| `Shell/EditorStatusBarController.cpp` | 绘制状态栏、dirty 状态、通知摘要等。 | 不执行业务操作。 |

## Widgets

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Widgets/EditorActionWidgets.h` | 声明 action 相关复用 UI widget。 | 只通过 action/command 接口触发行为。 |
| `Widgets/EditorActionWidgets.cpp` | 实现 action button/menu item 等控件。 | 不依赖具体 panel。 |
| `Widgets/EditorButtonWidgets.h` | 声明 Editor button 视觉参数和控件。 | 只放可复用按钮 UI。 |
| `Widgets/EditorButtonWidgets.cpp` | 实现按钮绘制。 | 不写业务事件。 |
| `Widgets/EditorTooltipWidgets.h` | 声明 tooltip 辅助。 | 只放 tooltip UI。 |
| `Widgets/EditorTooltipWidgets.cpp` | 实现 tooltip 绘制。 | 不依赖具体服务。 |
| `Widgets/EditorThemeColors.h` | 声明主题语义色查询（文本/强调/行 hover/drop zone 等）与选中按钮样式 push/pop。 | 面板取色统一走这里，不各自硬编码颜色。 |
| `Widgets/EditorThemeColors.cpp` | 实现主题色计算。 | 无状态；不做主题切换逻辑。 |
| `Widgets/EditorTreeWidget.h` | 声明复用树控件、拖拽视觉和状态。 | 可服务 hierarchy/asset tree，不绑定具体数据源。 |
| `Widgets/EditorTreeWidget.cpp` | 实现树节点绘制、拖拽槽、展开状态。 | 数据变更由调用者处理。 |
| `Widgets/InspectorPropertyWidgets.h` | 声明 Inspector 字段编辑 widget。 | 只处理字段 UI 和输入结果。 |
| `Widgets/InspectorPropertyWidgets.cpp` | 实现 property 行、数值、文本、checkbox 等控件。 | 不提交命令。 |
| `Widgets/InspectorAssetPathWidgets.h` | 声明资产路径字段控件（路径展示、选择、清除）。 | 只处理资产路径 UI，读 AssetDatabase 不写。 |
| `Widgets/InspectorAssetPathWidgets.cpp` | 实现资产路径控件绘制与候选查询。 | 不提交命令，不做资产 IO。 |
| `Widgets/ViewportAxisIndicator.h` | 声明 viewport 坐标轴指示器。 | 只放绘制参数和入口。 |
| `Widgets/ViewportAxisIndicator.cpp` | 绘制 viewport 右上角轴向指示。 | 不处理相机输入。 |

## Shaders

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `Shaders/CodexLogoDemo.hlsl` | Editor 专属 logo/demo shader。 | 不作为 engine runtime shader 依赖。 |
| `Shaders/CodexLogoComputeDemo.hlsl` | Editor 专属 compute demo shader。 | 不作为通用渲染管线接口。 |

## ThirdParty

| 文件 | 职责 | 修改约束 |
| --- | --- | --- |
| `ThirdParty/StbImageImpl.cpp` | 提供 stb image 单实现编译单元。 | 不写 Editor 业务；避免重复定义 stb implementation。 |
