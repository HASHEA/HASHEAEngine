# AshEngine Scene 到渲染链路设计

日期：2026-04-16

## 适用范围

本文档定义 AshEngine 第一版真正可用的 `Scene -> Render` 渲染流水线。

目标架构方向参考 UE，但第一阶段的功能范围会刻意收敛，只打通主链路：

- 仅支持静态 Mesh 场景渲染
- 单主相机 View
- `Scene` 归逻辑线程持有和更新
- 依赖 worker 线程协助做提取与视锥剔除
- 仅允许渲染线程提交 draw

这套设计必须满足以下约束：

- 保持 Engine / Editor 边界清晰
- 兼容 AshEngine 当前的线程模型
- 兼容当前 `Renderer` / `RenderDevice` 的高层提交方式

## 当前现状

当前引擎已经具备一些必要前置条件：

- `Scene` 已经是基于 `entt` 的 CPU 侧逻辑世界 façade
- `AssetDatabase` 已经能在 worker 线程加载 `Model` / `Mesh` / `AshAsset`
- 引擎已有第一阶段线程模型：`Render` / `Logic` / `Worker`
- 跨线程的 render work 已有 `enqueue_render_command()`
- `Renderer` 和 `RenderDevice` 已经具备 pass-based 的绘制提交路径

但目前真正缺失的是把这些东西连起来的桥接层：

- 没有 render-side scene representation
- 没有 render proxy
- 没有 view extraction 层
- 没有 visibility system
- 没有由逻辑线程构建、再交给渲染线程消费的稳定 frame packet
- 没有从 CPU 资产到可复用 GPU 渲染资源的桥接层

## 目标

第一阶段实现必须满足以下目标：

1. 让逻辑 `Scene` 真正变得可渲染，同时不把 RHI 细节暴露出 Engine 内部。
2. 保持 `Scene` 仍然是逻辑线程持有的 source of truth。
3. 禁止 render thread 直接读取 `Scene` / `entt` 的 live 状态。
4. 引入一条更接近 UE 风格的桥接链路：
   - `SceneProxy`
   - `PrimitiveSceneProxy`
   - `RenderScene`
   - `SceneView`
   - `VisibleRenderFrame`
   - `SceneRenderer`
5. 支持 CPU 多线程视锥剔除。
6. 保持 Vulkan 和 DX12 走同一条高层渲染路径。
7. 给未来系统预留扩展点：
   - 材质
   - 灯光
   - 实例化
   - 动画
   - shadow view
   - GPU-driven 渲染

## 第一阶段非目标

以下系统明确不在第一阶段完成：

- skeletal mesh
- animation evaluation
- 完整灯光渲染
- 阴影渲染
- 动态材质实例
- instancing / HISM / Nanite 风格聚合
- occlusion culling
- 完整 UE 深度的 pass processor / mesh draw command 架构

这些内容本阶段只需要预留扩展位，不要求完整实现。

## 推荐架构

本次选定的总体架构是：

- `Scene` 继续作为逻辑世界
- 在其上新增独立的 render-facing `RenderScene`
- 从逻辑 Scene 中提取 `PrimitiveSceneProxy`
- 从主相机构建 `SceneView`
- 在逻辑侧结合 worker 线程做 visibility
- 构建一份不可变的 `VisibleRenderFrame`
- 把它交给 render thread
- 由独立的 `SceneRenderer` 把它翻译为当前 `Renderer` / `RenderDevice` 可消费的 draw submission

这是当前最合适的中间路线：

- 比“直接把渲染缓存塞进 `Scene`”干净得多
- 比“第一版就做完整 UE 的 mesh draw command 体系”小得多

## 高层数据流

### 1. Logical World

`Scene` 继续作为 gameplay / editor-facing 的权威世界状态。

Entity 仍然通过逻辑组件表达状态，例如：

- `NameComponent`
- `TransformComponent`
- `CameraComponent`
- `LightComponent`
- `MeshComponent`

第一阶段只在必要范围内扩展 scene-side 数据模型，以支持静态 Mesh 渲染链路。

### 2. Render Asset Resolution

当前 `MeshComponent` 只保存：

- `asset_path`
- `mesh_index`
- `visible`

这对真正渲染来说是不够的。需要新增一层 render-resource bridge，把这些逻辑引用解析为可复用的 GPU-ready render asset。

建议新增类型：

- `StaticMeshRenderAsset`
- `StaticMeshRenderResource`
- `RenderAssetManager`

职责：

- 向 `AssetDatabase` 请求 CPU 资产数据
- 准备 section / bounds / material-slot 元数据
- 在 render thread 上创建并缓存 GPU vertex/index buffer
- 给 scene proxy 暴露稳定的 render-resource handle

### 3. Scene Proxy Layer

在逻辑 Scene 和 render-thread draw submission 之间增加一层 render extraction。

建议新增类型：

- `SceneProxy`
- `PrimitiveSceneProxy`
- `StaticMeshPrimitiveProxy`

职责：

- 只表示 render-relevant state
- 让 render-facing 数据脱离对 `entt` 的直接访问
- 缓存 primitive bounds 和 transform 派生出的 render state
- 把 mesh asset reference 解析为 render asset reference

第一阶段只需要静态 Mesh primitive，但基础 proxy 抽象需要为未来保留扩展空间：

- light proxy
- decal proxy
- skeletal mesh proxy
- particle proxy

### 4. RenderScene

`RenderScene` 是和某个逻辑 Scene 对应的 render-facing world representation。

职责：

- 持有 primitive proxy
- 维护稳定的 primitive 标识
- 跟踪来自逻辑 Scene 的 add / remove / dirty 状态
- 为 extraction / culling 提供紧凑的数据数组

重要边界：

- `RenderScene` 不是 RHI scene
- 它仍然是 Engine Function 层中的 render-world 抽象
- 它不能暴露 Vulkan / DX12 类型

### 5. SceneView

新增一层 view 抽象，让 visibility 和 scene render path 不再依赖随手拼出来的相机数据。

建议新增类型：

- `SceneViewDesc`
- `SceneView`
- `SceneViewFamily`

第一阶段要求：

- 单主相机
- view matrix
- projection matrix
- view-projection matrix
- camera position
- frustum planes
- viewport size

未来扩展位：

- 多 editor/game viewport
- shadow view
- reflection view
- stereo view

### 6. Visibility System

新增一个作用于 render primitive，而不是原始 scene entity 的 visibility 阶段。

建议新增类型：

- `VisibilityQuery`
- `VisiblePrimitiveSet`
- `VisibilityResult`

第一阶段行为：

- 仅做 CPU frustum culling
- 通过 worker 线程按 primitive range 分块并行
- 输出稳定的 per-view visible primitive list

这一阶段应消费：

- `RenderScene`
- `SceneView`

并输出：

- 当前帧可见 primitive 的紧凑列表

### 7. VisibleRenderFrame

这是最关键的跨线程 handoff 对象。

建议新增类型：

- `VisibleRenderFrame`

设计规则：

- 构建完成后不可变
- 能安全地从 logic thread 交给 render thread
- 只包含 render thread 真正需要的数据
- 不依赖 `Scene` 或 `entt` 在 render thread 上仍可读

第一阶段它至少应包含：

- frame index / generation
- 对应 scene / view 标识
- visible primitive entries
- 已解析的 world transform
- 已解析的 mesh sections
- 已解析的 render resources
- 未来材质绑定的预留位
- opaque static mesh 的 pass 分类信息

render thread 应只消费这份 packet，而不是回头再访问逻辑 Scene。

### 8. SceneRenderer

不要把 `Renderer` 自己变成一个 world manager。

建议入口：

- 新增独立 `SceneRenderer`

职责：

- 接收 `VisibleRenderFrame`
- 构建 scene rendering 所需 pass
- 把 visible primitive 翻译成当前已有的 `GraphicsDrawDesc`
- 选择第一阶段使用的 graphics program
- 通过现有 `Renderer` facade 执行 draw submission

相比把所有逻辑直接塞进 `Renderer`，这种做法更合适，因为：

- `Renderer` 仍可保持通用 render facade 身份
- scene rendering 会成为一个更高层的系统，而不是污染基础 renderer
- 未来保留非 scene 类渲染调用路径
- 更符合你要求的 UE 风格方向

## 线程归属模型

### Logic Thread

持有：

- `Scene`
- 场景更新与实例化
- proxy dirty tracking
- view 构建
- visibility orchestration
- `VisibleRenderFrame` 构建

禁止：

- 直接发 RHI 调用
- 直接修改 render-thread-owned GPU 资源

### Worker Threads

负责：

- async CPU asset loading
- section / bounds 准备
- visibility 分块任务
- 不直接修改 render state 的 CPU extraction 辅助任务

禁止：

- 直接碰 `Renderer`
- 直接提交 draw

### Render Thread

持有：

- `Renderer`
- `RenderDevice`
- `GraphicsProgram` / `ComputeProgram` 绑定与 draw submission
- render asset 的 GPU resource 创建与 finalize
- 已完成 `VisibleRenderFrame` 的消费

禁止：

- 直接读取 `Scene` / `entt`
- 在提交 draw 时依赖 live 的逻辑世界状态

## 同步模型

第一阶段应该使用一套简单、明确、易调试的同步模型。

推荐方式：

- logic thread 构建完整 `VisibleRenderFrame`
- 通过现有 render command queue 把它交给 render thread
- render thread 消费最新完成的 frame packet

可接受的具体实现有两种：

1. 基于 render-command 的 shared frame packet handoff
2. 带 generation number 的双缓冲 latest-frame exchange

第一阶段建议优先采用：

- 基于 render-command 的不可变 shared frame packet handoff

原因：

- 和当前 `enqueue_render_command()` 自然兼容
- 调试简单
- 容易 fence
- 比较适合在现有引擎上先落地

以后如果有必要，可以在不推翻 proxy/view/culling 架构的前提下，把 frame packet transport 再升级。

## Scene 侧需要补的数据

第一阶段对 scene 数据的扩展应保持克制，只补架构上必要的内容。

建议新增：

- 面向 static mesh 的 render metadata 来源数据
- mobility / static 标记的预留位
- layer / filter 的预留位

不要把 raw GPU resource handle 直接放进 `MeshComponent`。

`MeshComponent` 仍应保持逻辑引用组件的定位。

任何 GPU-facing 的解析都应放在 render asset / proxy 这一层完成。

## Asset 与 Render Resource 模型

第一阶段静态 Mesh 渲染需要一套可复用的 render asset system。

建议资源分层：

- CPU import data 继续留在 `AssetData`
- render-ready mesh resource 新增到 render asset 层

建议对象：

- `StaticMeshRenderAsset`
  - engine-facing 的静态 Mesh 资产记录
  - 持有 CPU 侧 mesh section 元数据和逻辑资产标识

- `StaticMeshRenderResource`
  - 已解析的 GPU resource bundle
  - 持有 vertex buffer、index buffer、section draw range、bounds

- `RenderAssetManager`
  - cache 与生命周期管理器
  - 接收逻辑 mesh reference
  - 跟踪 load state
  - 安排 render-thread resource finalize

这一层将作为以下系统之间的桥：

- `AssetDatabase`
- `SceneProxy`
- `SceneRenderer`

## 提交路径

第一阶段不应该推翻现有 pass / draw 路径，而是复用它。

推荐流程：

1. `SceneRenderer` 创建或复用第一阶段静态 Mesh 使用的 graphics program。
2. `SceneRenderer` 构建一个输出到 engine back buffer 的 opaque scene pass。
3. 对每个 visible primitive section：
   - 解析 vertex/index buffer
   - 设置 transform / object constant
   - 设置材质状态预留位
   - 组出一条 `GraphicsDrawDesc`
4. 通过 `Renderer::GraphicsPassContext` 提交

这样可以在保持现有 renderer 路径不变的前提下，把 scene extraction 和 scene submission 的缺失层补起来。

## 未来扩展位

第一阶段实现中，应明确为以下系统保留挂点：

- `LightSceneProxy`
- `MaterialRenderProxy`
- `MeshPassType`
- `RenderLayerMask`
- `PrimitiveVisibilityFlags`
- instancing groups
- shadow view family
- pass processor / draw command compaction

如果有助于稳定类型边界，这些对象可以先以空实现或最小实现存在。

## 文件与模块影响范围

以下已有文件很可能需要修改：

- `project/src/engine/Function/Scene/SceneComponents.h`
- `project/src/engine/Function/Scene/Scene.h`
- `project/src/engine/Function/Scene/Scene.cpp`
- `project/src/engine/Function/Asset/AssetData.h`
- `project/src/engine/Function/Asset/AssetData.cpp`
- `project/src/engine/Function/Asset/AssetDatabase.h`
- `project/src/engine/Function/Asset/AssetDatabase.cpp`
- `project/src/engine/Function/Render/Renderer.h`
- `project/src/engine/Function/Render/Renderer.cpp`

建议新增的 Engine Function 层模块：

- `project/src/engine/Function/Render/SceneRenderer.*`
- `project/src/engine/Function/Render/RenderScene.*`
- `project/src/engine/Function/Render/SceneProxy.*`
- `project/src/engine/Function/Render/SceneView.*`
- `project/src/engine/Function/Render/Visibility.*`
- `project/src/engine/Function/Render/RenderAssetManager.*`
- `project/src/engine/Function/Render/StaticMeshRenderAsset.*`

具体文件拆分在 implementation plan 阶段还可以微调，但职责边界不应混掉。

## 推荐实现顺序

1. 引入静态 Mesh 的 render asset bridge。
2. 引入 `RenderScene` 和 static mesh primitive proxy。
3. 增加 render primitive 的 transform / bounds extraction。
4. 增加 `SceneView` 和单相机提取。
5. 增加多线程 frustum culling。
6. 增加不可变 `VisibleRenderFrame` handoff。
7. 增加 `SceneRenderer` 并接入现有 `Renderer`。
8. 增加 Sandbox 对 scene rendering 的覆盖。
9. 跑 Vulkan + DX12 双后端验证。

## 验证策略

第一阶段验证应至少覆盖：

- CPU 资产加载与 scene instantiation 仍然正常
- 静态 Mesh scene 能从 logic-thread-driven 路径渲染出来
- visibility 能正确剔除视锥外 primitive
- render thread 不再依赖直接访问 `Scene`
- Vulkan 和 DX12 能走同一条 scene render path
- 退出时无 validation / leak / shutdown regression

推荐验证路径：

- 先给 Sandbox 增加 scene-render smoke test
- 再跑标准 AshEngine 双后端 validation loop

## 最终选定方向

当前已确认采用的方向为：

- UE 风格架构
- 独立 `SceneRenderer`
- 独立 `RenderScene`
- 独立 proxy / view / visibility / frame-packet 流水线
- 第一阶段功能只先覆盖静态 Mesh 渲染
- 复杂系统只做扩展位，不在本阶段实现

这就是后续 implementation plan 的基线。
