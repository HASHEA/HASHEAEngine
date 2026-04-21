# SceneRenderer 多 View 提交设计

**状态：** 提议  
**日期：** 2026-04-21  
**范围：** `project/src/engine/Function/Render`，以及少量 `project/src/sandbox` 适配点和 `docs/EngineDeveloperGuide.md`

## 1. 目标

把当前 `SceneRenderer` 只适合“单主相机、单输出目标”的第一阶段实现，收口成一套可支持 **同一帧连续提交多个 `SceneView`** 的 Engine 能力。

这次改动的第一版目标明确限定为：

- 同一帧内允许连续提交多个 `SceneView`
- 每个 `view` 都有自己独立的：
  - `output_target`
  - `depth_target`
  - `viewport`
  - `scissor`
- `SceneRenderer` 不再持有“一张全局唯一 depth RT”的隐式假设
- 当前单 `view` 路径继续可用
- 不为这次改动额外增加一个 Sandbox 多视口演示模式

这里的重点不是“把多视口 UI 做出来”，而是先把 **Scene 可见数据** 和 **per-view render attachments / raster state** 的职责边界拉清楚。

## 2. 为什么要改

当前实现里，`SceneRenderer` 作为 `Application` 生命周期内的单实例对象，直接持有：

- `GraphicsProgram`
- `m_depth_target`

当前 `VisibleRenderFrame` 同时还持有 `output_target`。

这在第一阶段单主相机路径下能工作，但它把几个本该分开的概念耦合在一起了：

- scene 可见性与 draw 数据
- view 的输出附件
- view 的 depth 生命周期
- view 的 viewport / scissor 提交状态

现状的主要问题：

- `SceneRenderer` 的 `m_depth_target` 实际上是“全局 scratch depth”，但接口没有表达这一点
- 如果同一帧连续提交多个不同 `view`，depth 资源归属不清晰
- `VisibleRenderFrame` 把逻辑线程构建的数据和渲染线程决定的输出目标绑在一起，所有权模型混乱
- 当前 pass 默认整 attachment clear，这对未来多个子视口共享同一输出时的行为边界不够明确

换句话说，现在的问题不只是 “`depthRT` 会不会冲突”，而是 **它现在放错了抽象层**。

## 3. 推荐方向

本次采用 **per-view render context** 方案，而不是继续在 `SceneRenderer` 内补全局缓存。

推荐方向：

- `VisibleRenderFrame` 只保留 scene-visible 数据，不再持有 `output_target`
- 新增一个独立的 per-view 提交描述类型，暂定名：
  - `SceneRenderViewContext`
- `SceneRenderer` 的提交接口改成：
  - `render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context)`
- `SceneRenderer` 只保留跨 `view` 可复用状态：
  - graphics program
  - 轻量内部 scratch depth 获取逻辑
- 调用方每次提交一个 `view` 时，显式提供该 `view` 的：
  - 输出目标
  - depth 目标或 depth 获取策略
  - viewport / scissor
  - load/clear 配置

这条路线的原因：

- 它能正面解决 `depthRT` 归属问题
- 它不会把当前第一阶段实现扩大成完整的 `SceneViewFamily` 二次重构
- 它能同时支持：
  - 多个 game/editor viewport
  - 后续 shadow / reflection / offscreen 这类单独 view
- 它保持 `Renderer` 仍然是通用 facade，scene 渲染细节继续封装在 `SceneRenderer`

## 4. 功能需求

### 4.1 多 View 连续提交

第一版需要支持：

- 在 render thread 上同一帧连续多次调用 `SceneRenderer`
- 每次调用提交一个独立 `view`
- 不同 `view` 可以：
  - 输出到不同的 `RenderTarget`
  - 使用不同的 depth target
  - 使用不同的 viewport / scissor

这次不要求：

- 一次调用提交整个 `SceneViewFamily`
- 在逻辑侧批量构建一个“多 view frame family”
- 做 shadow / reflection 的完整生产链路

### 4.2 Per-View 输出目标

每个 `view` 都必须显式描述自己的 `output_target`。

要求：

- `output_target` 由调用方在 render-thread 提交阶段提供
- `VisibleRenderFrame` 不再保存 `output_target`
- `SceneRenderer` 不再隐式假定所有 scene 提交都写到同一个 back buffer

### 4.3 Per-View Depth 目标

每个 `view` 都必须有清晰的 depth 语义。

第一版要求支持两种模式：

1. **显式 depth target**
   - 调用方传入 `depth_target`
   - `SceneRenderer` 只消费，不接管生命周期
   - 适合需要跨 pass 保留 depth 的 `view`

2. **内部 scratch depth**
   - 调用方不传 `depth_target`
   - `SceneRenderer` 按当前 `view` 的输出尺寸和格式获取一张内部可复用 depth
   - 这张 depth 只保证本次 scene pass 可用，不提供持久化语义

### 4.4 Per-View Raster 状态

每个 `view` 需要支持独立的：

- viewport
- scissor
- color load action / clear color
- depth load action / clear value

要求：

- 如果未显式提供 viewport / scissor，则默认覆盖整个输出目标
- 如果提供了 viewport / scissor，则 draw 提交应按该区域进行光栅化

### 4.5 单 View 兼容性

当前默认单 `view` 路径必须继续可用。

这意味着：

- `Sandbox` 当前标准场景不用新增多视口演示，也必须能正常渲染
- 现有默认提交路径只需要补一个 full-target 的 `SceneRenderViewContext`
- 改动后不应要求逻辑线程理解新的多 `view` 调度模型

## 5. 架构设计

### 5.1 数据职责拆分

本次要把两个概念彻底拆开：

1. **`VisibleRenderFrame`**
   - 逻辑线程产出
   - 表示某个 `SceneView` 下已经完成可见性筛选和 draw 数据解析的 frame packet
   - 只包含 render thread 真正需要的可见 primitive / transform / render asset / section 数据

2. **`SceneRenderViewContext`**
   - 渲染线程提交时提供
   - 表示这个 `view` 要写到哪里、用什么 depth、用什么 raster 区域和 clear/load 规则

这样逻辑线程继续负责 scene/view/culling，render thread 继续负责真实提交目标和 pass 细节。

### 5.2 `VisibleRenderFrame` 的职责

`VisibleRenderFrame` 保留：

- `frame_index`
- view / projection / view_projection
- camera position
- static mesh draw 列表

`VisibleRenderFrame` 移除：

- `output_target`

设计原则：

- 它仍然是一份不可变 handoff packet
- 它不拥有任何“当前要画到哪张 RT”这种 render-thread 决策
- render thread 消费它时，不需要回头读逻辑 `Scene`

### 5.3 `SceneRenderViewContext` 的职责

新增 `SceneRenderViewContext`，建议至少包含：

- `const char* debug_name` 或轻量 `view_id`
- `std::shared_ptr<RenderTarget> output_target`
- `std::shared_ptr<RenderTarget> depth_target`
- `bool has_viewport`
- `RenderViewport viewport`
- `bool has_scissor`
- `RenderScissor scissor`
- `RenderLoadAction color_load_action`
- `RenderColorValue color_clear_value`
- `RenderLoadAction depth_load_action`
- `RenderDepthStencilValue depth_clear_value`

这层对象的定位是：

- 它不是逻辑 `SceneView`
- 它不是新的 world-facing scene abstraction
- 它是 render-thread scene pass submission descriptor

### 5.4 `SceneRenderer` 的职责

`SceneRenderer` 继续负责：

- 保证 scene static mesh program 可用
- 将 `VisibleRenderFrame` 翻译为 `GraphicsDrawDesc`
- 根据 `SceneRenderViewContext` 组装 pass
- 执行 scene opaque pass

`SceneRenderer` 不再承担：

- 持有全局唯一 scene depth target 的语义
- 从 `VisibleRenderFrame` 中反向读取 `output_target`

`SceneRenderer` 允许保留的内部状态：

- `GraphicsProgram`
- 内部 scratch depth 的获取与复用逻辑

这里的关键点是：**保留缓存，但不保留错误的所有权语义。**

### 5.5 Depth 资源规则

#### 显式 depth 模式

如果 `view_context.depth_target != nullptr`：

- 使用调用方提供的 depth
- `SceneRenderer` 不缓存、不替换、不重建这张 depth
- 这张 depth 的生命周期由调用方负责

#### Scratch depth 模式

如果 `view_context.depth_target == nullptr`：

- `SceneRenderer` 使用内部 scratch depth 逻辑
- scratch depth 至少按以下条件区分：
  - width
  - height
  - format
- 第一版允许 `SceneRenderer` 通过内部 cache 或 transient acquisition 获取这张 depth

第一版的 scratch depth 语义必须明确：

- 它只保证这一次 scene pass 可用
- 不保证可以被后续 pass 继续读取或复用为长期资源
- 如果调用方需要后续 pass 继续消费 depth，必须自己传入显式 `depth_target`

### 5.6 Viewport / Scissor 与 Clear 语义

第一版必须明确一个边界：

- `viewport` / `scissor` 只限制 draw 的光栅区域
- pass 的 `RenderLoadAction::Clear` 仍然是 attachment 级语义，不是 rect clear

因此如果多个 `view` 共享同一张 `output_target`，并且它们只写其中的子区域：

- 第一个 `view` 可以使用 `Clear`
- 后续 `view` 应该使用 `Load`
- 如果调用方需要“只清一个小窗区域”，那不是本次任务范围

这个约束要直接写进接口使用规则，避免以后把“局部清屏”错误地当成当前已经支持的能力。

## 6. 接口与文件影响范围

### 6.1 新增文件

建议新增：

- `project/src/engine/Function/Render/SceneRenderView.h`

职责：

- 放置 `SceneRenderViewContext`
- 避免把 per-view 提交数据继续堆进 `SceneRenderer.h`

### 6.2 修改 `RenderScene`

需要修改：

- `project/src/engine/Function/Render/RenderScene.h`
- `project/src/engine/Function/Render/RenderScene.cpp`

改动方向：

- `VisibleRenderFrame` 移除 `output_target`
- `build_visible_render_frame()` 去掉 `output_target` 参数
- 逻辑线程 frame build 只依赖 `SceneView`

### 6.3 修改 `SceneRenderer`

需要修改：

- `project/src/engine/Function/Render/SceneRenderer.h`
- `project/src/engine/Function/Render/SceneRenderer.cpp`

改动方向：

- 提交接口升级为 `render_visible_frame(frame, view_context)`
- pass 组装改为读取 `view_context`
- depth 获取逻辑改为“显式 depth 或 scratch depth”
- viewport / scissor 默认值与显式 override 规则收口到这一层

### 6.4 修改现有调用点

需要修改：

- `project/src/sandbox/App/SandboxStandardScene.cpp`
- `project/src/sandbox/App/SandboxApplication.cpp`

改动方向：

- `Sandbox` 继续只构建单 `view` 的 `VisibleRenderFrame`
- render submit 时补一个默认 full-target 的 `SceneRenderViewContext`
- 不新增专门的多视口展示逻辑

### 6.5 文档更新

需要更新：

- `docs/EngineDeveloperGuide.md`

至少应补充：

- `SceneRenderer` 已支持 per-view 提交上下文
- `VisibleRenderFrame` 不再持有输出目标
- 第一版多 `view` 的 clear / load / viewport 语义边界

## 7. 范围边界

### In Scope

- 去掉 `SceneRenderer` 对全局唯一 depth RT 的隐式假设
- 引入 per-view scene render submission context
- 让同一帧内连续提交多个 `SceneView` 成为合法用法
- 保持当前单 `view` 路径继续工作
- 更新相关 Engine 文档

### Out Of Scope

- 直接把 `VisibleRenderFrame` 升级成完整的 `SceneViewFamily`
- 一次调用提交整个 multi-view family
- shadow / reflection / portal / stereo 的完整系统
- rect clear / partial attachment clear
- 专门的 Sandbox 多视口演示 UI
- Editor 侧多 viewport 功能接入

## 8. 兼容性与限制

### 8.1 当前多 View 形态

本次“多 `view`”的定义是：

- 同一帧中多次构建 `VisibleRenderFrame`
- 同一帧中多次调用 `SceneRenderer::render_visible_frame`

而不是：

- 一次 frame packet 里内嵌多个 views
- 一次提交 API 自动遍历整个 family

### 8.2 共享输出目标的限制

如果多个 `view` 共享同一张 `output_target`：

- 必须由调用方正确设置每个 `view` 的 load action
- 不能假设 `Clear` 会只清 viewport/scissor 覆盖区域
- 如果使用错误的 clear/load 组合，后一个 `view` 覆盖前一个 `view` 的结果是预期内行为

### 8.3 Depth 持久化限制

如果调用方依赖 scene pass 之后继续读取 depth：

- 不能依赖 scratch depth
- 必须传显式 `depth_target`

## 9. 验证策略

本次属于共享渲染路径改动，最终按 Vulkan + DX12 双后端标准验收。

最低验证要求：

- 当前默认 `Sandbox` 标准场景在新接口下仍能正常提交和显示
- `SceneRenderer` 的新接口能在单 `view` 路径上稳定工作
- `VisibleRenderFrame` 与 `SceneRenderViewContext` 的职责边界在代码层面真正落地

最终验收矩阵：

- `Sandbox` on `Vulkan`
- `Sandbox` on `DX12`
- `Editor` on `Vulkan`
- `Editor` on `DX12`

这次不要求新增一个可见的 Sandbox 多视口演示模式；验收重点是 Engine 能力和现有路径回归稳定性。

## 10. 最终选定方向

本次最终选定：

- 不做最小补丁式的全局 depth map 修补
- 不做一步到位的 `SceneViewFamily` 重构
- 采用 **per-view render context** 方案

最终结构为：

- `VisibleRenderFrame` 只表示 scene-visible draw packet
- `SceneRenderViewContext` 表示 per-view 输出与 raster 提交上下文
- `SceneRenderer` 只保留可跨 view 复用的 scene submission 状态

这样既能解决当前 `SceneRenderer` 持有 `depthRT` 的设计问题，也能为未来多个 game/editor viewport、shadow view、reflection view 预留干净扩展位，而不会把这次改造扩大成完整 scene render architecture 的二次重写。
