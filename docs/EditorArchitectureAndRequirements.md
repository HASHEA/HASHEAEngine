# AshEngine Editor 架构与需求文档

> 本文档只基于当前实际参与编译的主线代码：
> `project/src/engine` + `project/src/editor`
>
> `KEnginePub` 仅作为历史参考目录，不纳入当前编辑器架构判断，也不作为后续实现的直接依赖。

## 1. 当前实际架构概览

当前工程的有效主线可以分为 4 层：

### 1.1 Base 基础设施层

目录：

- `project/src/engine/Base`

职责：

- 日志系统
- 内存分配
- 文件系统
- Service 管理
- 窗口与平台封装
- 基础数据结构

现状判断：

- 这一层已经具备引擎底座雏形。
- 但它仍偏“底层工具层”，距离编辑器级平台还缺少输入系统、事件系统、命令系统、任务系统、反射系统等基础能力。

### 1.2 Graphics / RHI 层

目录：

- `project/src/engine/Graphics`

职责：

- 图形上下文抽象
- Swapchain
- Buffer / Texture / Shader / Pipeline 等 GPU 资源
- Vulkan 后端实现

现状判断：

- 当前后端实际以 Vulkan 为主。
- 抽象已经存在，但现阶段仍以“能跑通渲染”为核心，尚未形成适配编辑器高级功能的完整渲染平台。

### 1.3 Function 层

目录：

- `project/src/engine/Function`

职责：

- 应用生命周期管理
- 渲染功能层封装
- 向 Editor 暴露稳定的高层接口

现状判断：

- 这是当前最有价值的一层。
- `Application -> Renderer -> RenderDevice -> Graphics/RHI` 的分层已经成立。
- 这条链路非常适合作为未来编辑器依赖的“唯一引擎入口”。

### 1.4 Editor 层

目录：

- `project/src/editor`

职责：

- 编辑器主程序
- 编辑器渲染逻辑
- 未来的面板、视口、场景编辑、资源管理、工具系统

现状判断：

- 当前 `Editor` 仍是一个启动壳。
- 现有功能基本只有 demo 级别的渲染示例。
- 还没有真正意义上的编辑器架构。

## 2. 当前架构结论

当前工程可以概括为一句话：

**有运行时骨架，有渲染抽象雏形，有编辑器入口，但还没有编辑器平台架构。**

具体判断如下：

### 2.1 当前已有的优点

- 已经开始做清晰分层，Editor 不需要直接依赖 Vulkan 细节。
- `Renderer` 已经具备资源创建、Pass、Draw、Dispatch 的高层接口。
- 顶层构建清晰，`Engine` 和 `Editor` 的编译关系明确。
- 编辑器未来可以沿着 `Function` 层继续演进，而不是直接侵入 `Graphics` 层。

### 2.2 当前存在的关键缺口

- 没有 Scene / Entity / Component 的正式编辑模型。
- 没有 Asset Database。
- 没有 Undo / Redo。
- 没有反射系统和 Inspector 驱动机制。
- 没有序列化规范。
- 没有编辑器 UI 框架落地。
- 没有项目系统、资源导入系统、Prefab 系统。
- 没有 Play / Edit 模式隔离。

### 2.3 当前最重要的判断

如果目标是未来做到接近 Unity 的工作流，那么当前阶段最需要优先建设的不是“多做几个面板”，而是先建立以下四个底座：

1. 场景数据模型
2. 资产数据库
3. 命令系统与撤销重做
4. 反射与序列化体系

没有这四个底座，后续任何 UI 都只能停留在 demo 工具层。

## 3. 编辑器总体目标

目标不是一次性做成 Unity，而是按可扩展架构逐步演进，形成：

- 清晰的运行时与编辑器边界
- 可扩展的工具系统
- 可持续增长的面板与工作流
- 可演进的资源、场景、序列化和插件体系

最终方向：

- 可视化场景编辑
- 资源导入与管理
- Inspector 驱动的属性编辑
- Prefab / Play Mode / Debug / Profiling / Build Pipeline
- 插件化扩展

## 4. 架构原则

后续实现编辑器时，建议严格遵守以下原则：

### 4.1 分层原则

- Editor 只依赖 `Function` 层公开能力。
- Editor 不直接依赖 `Graphics` 具体后端实现。
- Base 层只提供基础设施，不承载编辑器业务逻辑。

### 4.2 UI 原则

- UI 框架只是视图层，不允许成为整个编辑器架构本身。
- 面板、工具、文档对象、命令对象必须与具体 UI API 解耦。

### 4.3 数据原则

- 运行时数据与编辑器数据要分层。
- Scene、Asset、Prefab、Settings 都必须可序列化。
- 所有编辑行为应通过命令系统驱动，以支持 Undo / Redo。

### 4.4 扩展原则

- 面板、工具、导入器、组件绘制器都应按模块化设计。
- 后续支持插件时，已有模块边界应能自然迁移。

## 5. 框架选型建议

第一阶段建议采用：

- **Dear ImGui + Docking**

原因：

- 当前仓库已经包含 ImGui 依赖，接入成本低。
- 非常适合从 0 到 1 快速搭建编辑器工具界面。
- 有利于尽快验证架构、工作流和核心系统。

注意：

- 我们应当把 ImGui 包在编辑器框架内部。
- 不应把全项目写成“到处都是 ImGui 调用”的散乱结构。

建议额外抽象：

- `EditorWindow`
- `Panel`
- `DockWorkspace`
- `EditorStyle`
- `EditorCommand`

## 6. 需求列表

以下需求按优先级分阶段推进。

---

## 6.1 P0：编辑器骨架阶段

目标：

先把编辑器做成“可持续扩展的平台”，而不是临时 demo。

需求：

- 建立 `EditorCore` 核心模块
- 建立 `EditorApp` 启动与生命周期层
- 建立面板系统
- 建立主菜单和停靠布局系统
- 建立视口面板
- 建立控制台面板
- 建立层级面板
- 建立 Inspector 面板
- 建立资源浏览器面板
- 建立 SelectionService
- 建立 EditorSettingsService
- 建立 CommandService
- 建立 UndoRedoService
- 建立快捷键与输入映射系统
- 建立布局保存与恢复
- 建立渲染到独立 RenderTarget 的 Viewport 机制

交付标准：

- 编辑器能打开并显示多面板布局
- 可以在 Viewport 中显示场景渲染结果
- 可以选中对象并在 Inspector 中显示基础信息
- 布局重启后可恢复

---

## 6.2 P1：场景与资源基础阶段

目标：

让编辑器具备“编辑内容”的最小闭环。

需求：

- 建立 `Scene` 数据结构
- 建立 `Entity` 封装
- 建立 `Component` 基础体系
- 建立基础组件：
  - `TransformComponent`
  - `NameComponent`
  - `CameraComponent`
  - `MeshComponent`
  - `LightComponent`
- 引入 `entt` 作为 ECS 基础实现
- 建立场景序列化与反序列化
- 建立组件反射描述
- 建立 Inspector 自动绘制基础能力
- 建立 Gizmo：
  - 平移
  - 旋转
  - 缩放
- 建立 Asset Database
- 建立资源 GUID 系统
- 建立资源元数据文件
- 建立资源扫描与索引缓存
- 建立基础导入器：
  - Texture
  - Mesh
  - Material
  - Shader
- 建立 Shader 热重载

交付标准：

- 可以创建、保存、打开场景
- 可以创建实体并添加基础组件
- 可以在 Inspector 编辑基础属性
- 可以浏览资源并加载到场景中

---

## 6.3 P2：编辑器工作流阶段

目标：

让编辑器从“能编辑”升级为“能高效工作”。

需求：

- 建立 Prefab 系统
- 建立场景脏标记系统
- 建立保存确认与自动保存机制
- 建立 Play / Edit 模式切换
- 建立运行时世界与编辑器世界隔离
- 建立材质编辑器
- 建立场景相机控制
- 建立网格、坐标轴、辅助线、包围盒等调试绘制
- 建立 Profiler 面板
- 建立 Render Stats 面板
- 建立项目设置面板
- 建立最近项目、打开项目、新建项目工作流
- 建立日志过滤、搜索、跳转

交付标准：

- 编辑器具备稳定的日常内容制作流程
- 资源、场景、运行调试之间能闭环协作

---

## 6.4 P3：向 Unity 级架构演进阶段

目标：

为长期发展预留高扩展性能力。

需求：

- 插件系统
- 模块热插拔
- 脚本系统
- 动画系统
- Timeline / Sequencer
- 状态机编辑器
- 可视化材质图
- 可视化渲染图
- Build Pipeline
- 包管理器
- Source Control 集成
- 自动化测试工具链
- 大型项目支持能力

交付标准：

- 编辑器从“工具集合”演进为“可扩展的平台产品”

## 7. 推荐目录结构

建议后续把 `project/src/editor` 逐步演进为：

```text
project/src/editor/
├── App/
│   ├── EditorApplication.h
│   └── EditorApplication.cpp
├── Core/
│   ├── EditorContext.h
│   ├── EditorModule.h
│   ├── EditorWindow.h
│   ├── EditorCommand.h
│   └── EditorSelection.h
├── Services/
│   ├── SelectionService.h
│   ├── CommandService.h
│   ├── UndoRedoService.h
│   ├── AssetDatabaseService.h
│   ├── SceneService.h
│   └── EditorSettingsService.h
├── Panels/
│   ├── SceneHierarchyPanel.h
│   ├── InspectorPanel.h
│   ├── ViewportPanel.h
│   ├── AssetBrowserPanel.h
│   └── ConsolePanel.h
├── Scene/
│   ├── Scene.h
│   ├── Entity.h
│   ├── Components.h
│   └── SceneSerializer.h
├── Assets/
│   ├── AssetHandle.h
│   ├── AssetMeta.h
│   ├── AssetDatabase.h
│   └── Importers/
├── Rendering/
│   ├── EditorViewportRenderer.h
│   ├── GizmoRenderer.h
│   └── DebugDrawRenderer.h
├── ImGui/
│   ├── ImGuiLayer.h
│   ├── ImGuiRenderer.h
│   └── ImGuiStyle.h
└── Shaders/
```

## 8. 第一阶段落地顺序

建议我们从以下顺序开始实施：

1. 建立 `EditorCore` 目录和模块骨架
2. 接入 ImGui Docking
3. 做主窗口与基础布局
4. 做 `ViewportPanel`
5. 做 `HierarchyPanel`
6. 做 `InspectorPanel`
7. 做 `SelectionService`
8. 做最小 Scene / Entity / Transform 系统
9. 做场景保存/加载
10. 再进入资源系统与 Gizmo

## 9. 当前执行决策

从现在开始，编辑器开发按以下路线推进：

- 忽略 `KEnginePub`
- 只基于 `project/src/engine` 和 `project/src/editor`
- 先完成 P0 编辑器骨架
- 再进入 P1 场景与资源基础阶段

这条路线的目标是：

**先做出可持续扩展的编辑器框架，再逐步增加 Unity 风格能力。**
