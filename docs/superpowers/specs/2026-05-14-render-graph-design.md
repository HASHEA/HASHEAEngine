# Render Graph 设计

## 目标

在 AshEngine 中接入第一版类似 UE Render Dependency Graph 的帧级渲染编排系统。第一阶段同时覆盖两个方向：

- 把当前 scene deferred 主路径迁移到 graph：`SceneGBufferPass -> SceneDeferredLightingAccumPass -> SceneDeferredCompositePass`。
- 建立可复用的 resource / lifetime / barrier compiler，为后续 post process、custom renderer 和 compute pass 接入打底。

第一版 `RenderGraph` 是 `Function/Render` 层的 Engine-facing 系统，位于 `Renderer` 之上，并通过现有 `Renderer / RenderDevice` 执行。它不是新的 RHI backend，也不把 Vulkan / DX12 细节暴露给 Editor、Sandbox 或更高层业务代码。

## 非目标

- 不实现 async compute 或 multi-queue。
- 不做任意 pass reorder；第一版按 add 顺序执行。
- 不做 transient resource aliasing 到同一物理 texture。
- 不实现 UE 风格 shader parameter struct 自动绑定。
- 不让 Editor 直接依赖 `Graphics/` 或 backend-specific RHI 类型。
- 不一次性废弃 `Renderer` 的旧 direct path；非 RDG 路径继续可用。

## 当前上下文

当前渲染栈已经具备 RDG 前身所需的大部分基础：

- `Renderer` 负责 frame orchestration、pass context、draw 收集、dispatch、frame stats 和 UI 提交。
- `RenderDevice` 负责 render target / buffer / sampler / program 创建，pass begin / end，pipeline variant，descriptor commit，以及 pass 外资源状态转换。
- `SceneRenderer` 当前手写 deferred 顺序：GBuffer pass、lighting accumulation pass、composite pass。
- `SceneDeferredResources` 持有 GBuffer、deferred depth 和 lighting accumulation 的 persistent render targets。
- Vulkan 合法性要求所有 resource barrier 都发生在 active render pass 之外，这应作为 graph compiler 的硬约束。

因此第一版 RDG 应该复用现有 pass/framebuffer cache、shader binding、program binding 和 backend resource tracker，而不是直接下沉到 `Graphics/` 重写后端。

## 推荐架构

新增 `RenderGraph` 模块，放在 `project/src/engine/Function/Render`：

```text
RenderGraph.h
RenderGraphFwd.h
RenderGraphResource.h/.cpp
RenderGraphPass.h/.cpp
RenderGraphBuilder.h/.cpp
RenderGraphCompiler.h/.cpp
RenderGraphExecutor.cpp
```

运行关系：

```text
ScenePresentationSubsystem
  -> SceneRenderer::render_visible_frame()
     -> RenderGraphBuilder graph(renderer)
        -> register_external_texture(view output)
        -> create_texture(SceneGBufferA...)
        -> add_raster_pass(SceneGBufferPass)
        -> add_raster_pass(SceneDeferredLightingAccumPass)
        -> add_raster_pass(SceneDeferredCompositePass)
        -> graph.execute()
           -> cull passes
           -> compile resource lifetimes
           -> compile pre-pass barriers and attachment final states
           -> execute via Renderer / RenderDevice
```

`RenderGraphBuilder` 是每帧临时对象。所有 graph transient 资源只在 `execute()` 范围内有效，业务代码不能在 graph 外保存真实 `RenderTarget`。

## API 形态

第一版 public API 应保持轻量：

```cpp
class RenderGraphBuilder
{
public:
    RenderGraphBuilder(Renderer& renderer, const char* name);

    RenderGraphTextureRef register_external_texture(
        const std::shared_ptr<RenderTarget>& texture,
        const char* name,
        RenderGraphAccess initial_access = RenderGraphAccess::Unknown);

    RenderGraphTextureRef create_texture(
        const RenderGraphTextureDesc& desc,
        const char* name);

    void extract_texture(RenderGraphTextureRef texture);

    bool add_raster_pass(
        const char* name,
        RenderGraphPassFlags flags,
        const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
        const std::function<bool(RenderGraphRasterContext&)>& execute);

    bool add_compute_pass(
        const char* name,
        RenderGraphPassFlags flags,
        const std::function<void(RenderGraphComputePassBuilder&)>& setup,
        const std::function<bool(RenderGraphComputeContext&)>& execute);

    bool execute();
};
```

Pass setup 阶段显式声明资源访问：

```cpp
pass.write_color(0, output, load_action, clear_color);
pass.write_depth(depth, depth_load_action, clear_depth);
pass.read_texture(gbuffer_a, RenderGraphAccess::GraphicsSRV);
pass.read_depth(depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
```

Execute 阶段只提交命令：

```cpp
context.draw(draw_desc);
context.dispatch(dispatch_desc);
context.get_texture(texture_ref);
```

`context.get_texture()` 只在 execute lambda 内有效，用于兼容当前 `GraphicsProgram::set_texture("Name", render_target)` 绑定方式。后续如果引入 RDG parameter block，再收紧这条兼容路径。

## Resource And Access Model

Graph resource 第一版只要求支持 texture；buffer 可以在 compute/custom pass 需求明确后补齐。Texture 分两类：

- External texture：由 graph 外部持有，例如 window output、offscreen viewport output、已有 persistent render target。
- Transient texture：由 graph 创建和管理，例如 GBuffer、deferred depth、lighting accumulation。

Engine-facing access 枚举建议：

```cpp
enum class RenderGraphAccess : uint16_t
{
    Unknown = 0,
    GraphicsSRV,
    ComputeSRV,
    GraphicsUAV,
    ComputeUAV,
    ColorAttachmentWrite,
    DepthStencilWrite,
    DepthStencilRead,
    VertexBufferRead,
    IndexBufferRead,
    ConstantBufferRead,
    CopySrc,
    CopyDst,
    Present
};
```

访问到 RHI state 的映射：

```text
GraphicsSRV            -> RHI::AshResourceState::SRVGraphics
ComputeSRV             -> RHI::AshResourceState::SRVCompute
GraphicsUAV            -> RHI::AshResourceState::UAVGraphics
ComputeUAV             -> RHI::AshResourceState::UAVCompute
ColorAttachmentWrite   -> RHI::AshResourceState::RTV
DepthStencilWrite      -> RHI::AshResourceState::DSVWrite
DepthStencilRead       -> RHI::AshResourceState::DSVRead
VertexBufferRead       -> RHI::AshResourceState::VertexBuffer
IndexBufferRead        -> RHI::AshResourceState::IndexBuffer
ConstantBufferRead     -> RHI::AshResourceState::ConstBuffer
CopySrc                -> RHI::AshResourceState::CopySrc
CopyDst                -> RHI::AshResourceState::CopyDst
Present                -> RHI::AshResourceState::Present
```

Depth read-only + shader sampling 是一个显式特例：lighting pass 可以把 depth 同时声明为 read-only depth attachment 和 `GraphicsSRV`，最终状态映射为 `DSVRead | SRVGraphics`。

## Pass Culling

第一版支持保守 pass culling：

- External output 默认是 culling root。
- `extract_texture()` 标记的资源是 culling root。
- `RenderGraphPassFlags::NeverCull` 的 pass 永远保留。
- 其他 pass 如果写出的资源不通向任何 root，就从 compiled graph 中剔除。
- 被剔除 pass 的 transient resource 不分配、不执行、不产生 barrier。

Compiler 可以在 Debug 日志中输出被剔除 pass 名称，帮助定位声明错误和无效工作。

## Compiler And Execution

`RenderGraphCompiler` 的步骤：

1. 收集 pass/resource usage。
2. 从 external output、extracted resource、`NeverCull` pass 反向标记 live pass。
3. 删除 dead pass 后重新计算每个 resource 的 first use / last use。
4. 为 live pass 生成 pre-pass barrier batch。
5. 为 raster attachment 生成 final state。
6. 为 transient texture 生成 allocate / release 点。
7. 输出 compiled graph 给 executor。

第一版按 add 顺序执行，不主动重排。Compiler 只做依赖校验和 culling；如果某个 pass 读取没有 producer 且不是 external 的资源，直接失败。

执行规则：

- Raster pass 的 barrier 必须在 `Renderer::begin_pass()` 前提交。
- Raster execute lambda 内禁止 dispatch、手动 barrier 和 nested pass。
- Compute pass 必须发生在没有 active raster pass 的区间。
- Culling 后没有 live pass 时，`execute()` 可以成功返回并记录 debug log。

## RenderDevice Integration

为了让 RDG 控制 attachment final state，`PassColorAttachment` 和 `PassDepthAttachment` 增加可选 final state：

```cpp
struct PassColorAttachment
{
    std::shared_ptr<RenderTarget> render_target;
    RenderLoadAction load_action;
    RenderColorValue clear_color;
    RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
};

struct PassDepthAttachment
{
    std::shared_ptr<RenderTarget> render_target;
    RenderLoadAction load_action;
    RenderDepthStencilValue clear_value;
    bool read_only = false;
    RHI::AshResourceState final_state = RHI::AshResourceState::Unknown;
};
```

`Unknown` 保持现有行为：`RenderDevice` 根据 back buffer、shader resource、unordered access、depth stencil 属性推导 final state。RDG 路径由 compiler 填入明确 final state。

再增加一个受控 barrier submit 入口，供 graph executor 在 pass 外提交 compiler 产出的 barrier。该入口仍在 Engine `Function/Render` 内部使用，不暴露给 Editor，也不暴露 backend-specific 类型给业务层。

非 RDG direct path 继续走当前 `Renderer / RenderDevice` 自动 barrier 逻辑，避免迁移期间全局行为回归。

## Scene Deferred Migration

`SceneRenderer::render_visible_frame()` 迁移后：

1. 创建 `RenderGraphBuilder`。
2. 注册 `view_context.output_target` 为 external output。
3. 创建 GBuffer textures、deferred depth、lighting accumulation transient textures。
4. 添加 `SceneGBufferPass`。
5. 调用 `DeferredLightingPass::add_passes(...)` 添加 lighting accumulation 和 composite pass。
6. 执行 graph。

`SceneDeferredResources` 逐步退役。第一阶段可以引入轻量的 graph resource 聚合：

```cpp
struct SceneDeferredGraphResources
{
    std::vector<RenderGraphTextureRef> gbuffer_targets;
    RenderGraphTextureRef depth;
    RenderGraphTextureRef lighting_accum;
};
```

`DeferredLightingPass::render()` 拆成 graph registration 函数，不再自己 begin/end pass：

```cpp
bool DeferredLightingPass::add_lighting_pass(
    RenderGraphBuilder& graph,
    const VisibleRenderFrame& frame,
    const SceneDeferredGraphResources& resources,
    const SceneRenderViewContext& view_context);

bool DeferredLightingPass::add_composite_pass(...);
```

Static mesh draw、material proxy、program binding 和 instance buffer 逻辑尽量不变，只把 pass context 换成 `RenderGraphRasterContext`。

## Validation Rules

第一版需要强校验：

- `GraphicsSRV` texture 必须支持 shader resource。
- UAV access 必须要求 resource 具备 unordered access 能力。
- Color attachment 不能使用 depth format。
- Depth attachment 必须使用 depth format。
- Read-only depth attachment 不能使用 `RenderLoadAction::Clear`。
- 同一 pass 内同一资源不允许普通 read/write 混用；唯一特例是 depth read-only attachment + shader SRV。
- Raster pass execute lambda 不能 dispatch。
- Compute pass execute lambda 不能 draw。
- Culled pass 不分配 transient resource，不提交 barrier。
- External output 自动成为 culling root。

Validation error 使用项目现有 process-error 风格收口，并输出清晰 pass/resource 名称。

## Implementation Plan

建议分阶段落地：

1. RDG 基础骨架：新增文件、handle、external/transient texture、raster/compute pass 声明和顺序执行。
2. Pass culling 与 lifetime compiler：实现 root 标记、live pass 反向遍历、first/last use、dead pass debug log。
3. Barrier compiler 接入：实现 access 到 `RHI::AshResourceState` 映射、attachment final state、pre-pass barrier submit。
4. Scene deferred 主路径迁移：用 graph 替换手写 GBuffer / lighting / composite 链路。
5. 清理与文档：降级或移除 `SceneDeferredResources`，补 RDG stats / log，更新 README 和 Engine developer guide。

## Validation Strategy

阶段 1 和 2 至少验证：

- `Sandbox --engine-self-test`
- Debug build
- `Sandbox + Vulkan` smoke
- `Sandbox + DX12` smoke

阶段 3、4、5 触碰 shared rendering path，需要完整矩阵：

- `Sandbox + Vulkan`
- `Sandbox + DX12`
- `Editor + Vulkan`
- `Editor + DX12`

阶段 4 重点检查：

- Sponza 默认场景正常显示。
- GBuffer -> lighting -> composite 内容正确。
- Vulkan validation 没有 active render pass 内 barrier 错误。
- DX12 debug layer 没有 resource state、clear value 或 descriptor 生命周期错误。
- resize、offscreen viewport、window output 正常。
- shutdown 无 Vulkan VMA leak 或 deferred descriptor pool 问题。

## Risks

主要风险是 RDG barrier 和旧自动 barrier 双重推导导致 final state 冲突。缓解策略：

- 非 RDG path 保持旧自动 barrier。
- RDG path 由 compiler 明确提供 pre-pass barrier 和 attachment final state。
- `RenderDevice` 的 `Unknown` final state 只服务旧 path；RDG pass 不依赖隐式猜测。

第二个风险是第一版 API 太接近 UE 全量 RDG，导致实现范围失控。缓解策略：

- 第一版只支持 texture graph、raster pass、compute pass、pass culling 和 barrier compile。
- 不做 async compute、multi-queue、aliasing、shader parameter struct 自动绑定。
- 先让 scene deferred 主路径真实跑通，再扩展 post process 和 custom renderer。
