# RenderGraph Usage Tutorial And API Spec

本文面向 Engine 侧渲染开发者，说明当前 AshEngine `RenderGraph` 的使用方式、API 语义和 pass 编写约束。

当前实现位于 `project/src/engine/Function/Render`，属于 Function/Render 层的帧级声明式编排系统。它运行在 `Renderer` / `RenderDevice` 之上，不是新的 RHI backend，也不向 Editor、Game 或 Client 暴露 Vulkan / DX12 细节。

## 1. 当前定位

`RenderGraph` 第一版负责：

- 声明 frame-local texture resource graph。
- 声明 raster / compute pass 的 texture 读写关系。
- 编译 live pass、transient texture lifetime 和 pass barrier plan。
- 通过现有 `Renderer` / `RenderDevice` 执行 pass、draw、dispatch 和 transient render target 分配。
- 保持 Vulkan / DX12 共享路径合法，尤其是 pass 外提交 resource transition。

它当前不负责：

- 管理 buffer、sampler、shader、pipeline、material 或 scene object lifetime。
- 自动生成 shader parameter block。
- 跨 frame 保存 graph transient 资源。
- async compute 调度、pass 重排、transient aliasing 或跨 queue 同步。

## 2. Include 与代码边界

普通使用者 include 聚合头：

```cpp
#include "Function/Render/RenderGraph.h"
```

只需要 forward declaration 时 include：

```cpp
#include "Function/Render/RenderGraphFwd.h"
```

规则：

- `RenderGraph` 只应在 Engine Function/Render 层使用。
- 不要从 Editor 或业务层直接 include 后端 RHI 头来补 graph 能力。
- 不要把 Tracy 头或 backend-specific 类型扩散到公共头。
- 新增或修改 graph pass、render pass、compute dispatch 或明确性能热点时，必须同步补 `Base/hprofiler.h` 的 Tracy scope / count / name 打点。

## 3. 基本工作流

一次 graph 是一个 frame / view 级临时对象：

```cpp
RenderGraphBuilder graph(*m_renderer, "SceneRenderGraph");
```

典型步骤：

1. 注册外部输出资源，例如 swapchain backbuffer、viewport render target 或 offscreen target。
2. 创建 graph transient textures，例如 GBuffer、depth、lighting accumulation。
3. 用 `add_raster_pass()` / `add_compute_pass()` 声明 pass。
4. 在 setup lambda 中声明所有 graph texture 读写。
5. 在 execute lambda 中通过 context 获取实际 `RenderTarget` 并提交 draw / dispatch。
6. 调用 `graph.execute()`。

最小 raster 示例：

```cpp
RenderGraphBuilder graph(*m_renderer, "ExampleGraph");

RenderGraphTextureRef output =
    graph.register_external_texture(view_context.output_target, "SceneOutput");

RenderGraphTextureDesc color_desc{};
color_desc.width = static_cast<uint16_t>(view_context.output_target->get_width());
color_desc.height = static_cast<uint16_t>(view_context.output_target->get_height());
color_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
color_desc.shader_resource = true;
color_desc.use_optimized_clear_value = true;
RenderGraphTextureRef lighting = graph.create_texture(color_desc, "ExampleLighting");

ASH_PROCESS_ERROR(graph.add_raster_pass(
    "ExampleLightingPass",
    RenderGraphPassFlags::None,
    [&](RenderGraphRasterPassBuilder& pass)
    {
        pass.write_color(0, lighting, RenderLoadAction::Clear, {});
    },
    [&](RenderGraphRasterContext& context) -> bool
    {
        ASH_PROFILE_SCOPE_NC("ExampleLightingPass", AshEngine::Profile::Color::Draw);
        GraphicsDrawDesc draw_desc{};
        draw_desc.program = lighting_program;
        draw_desc.vertex_count = 3;
        draw_desc.instance_count = 1;
        return context.draw(draw_desc);
    }));

ASH_PROCESS_ERROR(graph.add_raster_pass(
    "ExampleCompositePass",
    RenderGraphPassFlags::None,
    [&](RenderGraphRasterPassBuilder& pass)
    {
        pass.read_texture(lighting, RenderGraphAccess::GraphicsSRV);
        pass.write_color(0, output, view_context.color_load_action, view_context.color_clear_value);
    },
    [&](RenderGraphRasterContext& context) -> bool
    {
        ASH_PROFILE_SCOPE_NC("ExampleCompositePass", AshEngine::Profile::Color::Draw);
        ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
        std::shared_ptr<RenderTarget> lighting_target = context.get_texture(lighting);
        ASH_PROCESS_ERROR(lighting_target != nullptr);
        ASH_PROCESS_ERROR(composite_program->set_texture("SceneLighting", lighting_target));

        GraphicsDrawDesc draw_desc{};
        draw_desc.program = composite_program;
        draw_desc.vertex_count = 3;
        draw_desc.instance_count = 1;
        ASH_PROCESS_ERROR(context.draw(draw_desc));
        ASH_PROCESS_GUARD_RETURN_END(bResult, false);
    }));

ASH_PROCESS_ERROR(graph.execute());
```

## 4. Resource 模型

### 4.1 RenderGraphTextureRef

`RenderGraphTextureRef` 是 graph 内部 texture handle：

```cpp
struct RenderGraphTextureRef
{
    uint32_t index = UINT32_MAX;
    bool is_valid() const;
    explicit operator bool() const;
};
```

约束：

- 只在创建它的 `RenderGraphBuilder` 内有效。
- 不要跨 frame、跨 view 或跨 graph 缓存。
- execute lambda 中如果需要真实资源，使用 `context.get_texture(ref)`。
- `context.get_texture()` 返回的 `RenderTarget` 只应在当前 execute lambda 内使用，不要保存到 graph 外。

### 4.2 External Texture

```cpp
RenderGraphTextureRef register_external_texture(
    const std::shared_ptr<RenderTarget>& texture,
    const char* name,
    RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
```

语义：

- 把已有 `RenderTarget` 引入 graph。
- 常用于输出目标、history target、ScenePresentation offscreen target 等 graph 外拥有的资源。
- external texture 是 culling root。如果某个 pass 写入 external texture，该 pass 及其依赖会被保留。
- 传入空 texture 会记录错误并返回 invalid ref。

当前限制：

- `initial_access` 当前是 API 预留参数，不参与 compiler 状态推导。
- external texture lifetime 仍由外部系统负责，graph 不拥有它。

### 4.3 Transient Texture

```cpp
RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
```

语义：

- 创建 graph transient texture 描述。
- 执行时由 `Renderer::acquire_transient_render_target()` 分配真实 `RenderTarget`。
- graph 执行结束或执行失败时由 `Renderer::release_transient_render_target()` 释放。

当前限制：

- 第一版 executor 在 graph scope 内分配所有 live transients，并在 graph 结束统一释放。
- compiler 已计算 lifetime，但当前不要依赖 transient aliasing 或 pass 后立即释放。

### 4.4 Extract Texture

```cpp
void extract_texture(RenderGraphTextureRef texture);
```

当前语义：

- 将 transient texture 标记为 culling root。
- 用于保留写入该 texture 的 producer pass 及其依赖。

重要限制：

- 当前 API 不返回可持久保存的 `RenderTarget`。
- graph transient 仍会在 `execute()` 结束后释放。
- 如果输出必须跨 graph / 跨 frame 存活，应传入外部拥有的 render target，而不是依赖 `extract_texture()`。

## 5. Texture Desc

```cpp
struct RenderGraphTextureDesc
{
    uint16_t width = 1;
    uint16_t height = 1;
    RenderTextureFormat format = RenderTextureFormat::Unknown;
    bool shader_resource = true;
    bool unordered_access = false;
    bool use_optimized_clear_value = false;
    RenderColorValue optimized_clear_color{};
    RenderDepthStencilValue optimized_clear_depth_stencil{};

    static RenderGraphTextureDesc from_render_target_desc(const RenderTargetDesc& desc);
    RenderTargetDesc to_render_target_desc(const char* name) const;
};
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `width` / `height` | texture 尺寸，当前 desc 类型是 `uint16_t`，调用方需要先检查输出尺寸范围。 |
| `format` | 高层 `RenderTextureFormat`。depth target 使用 `D24_UNORM_S8_UINT` 或 `D32_SFLOAT`。 |
| `shader_resource` | 后续是否可作为 SRV 绑定。GBuffer / depth sampling 通常为 `true`。 |
| `unordered_access` | 是否需要 UAV 能力。compute write texture 时按需求开启。 |
| `use_optimized_clear_value` | 是否向后端传优化 clear value。 |
| `optimized_clear_color` | color target optimized clear。 |
| `optimized_clear_depth_stencil` | depth target optimized clear。 |

## 6. Access Spec

`RenderGraphAccess` 描述 graph texture 在某个 pass 中的访问状态：

| Access | RHI 状态 | 典型用途 |
| --- | --- | --- |
| `Unknown` | `Unknown` | 未指定或保留。不要在 pass 声明中主动使用。 |
| `GraphicsSRV` | `SRVGraphics` | raster pass 中以 shader resource 读取 texture。 |
| `ComputeSRV` | `SRVCompute` | compute pass 中以 shader resource 读取 texture。 |
| `GraphicsUAV` | `UAVGraphics` | graphics pipeline UAV 访问，当前慎用。 |
| `ComputeUAV` | `UAVCompute` | compute pass 写 UAV texture。 |
| `ColorAttachmentWrite` | `RTV` | raster pass color attachment 写入。由 `write_color()` 设置。 |
| `DepthStencilWrite` | `DSVWrite` | raster pass depth attachment 写入。由 `write_depth()` 设置。 |
| `DepthStencilRead` | `DSVRead` 或 `DSVRead | SRVGraphics` | read-only depth attachment。由 `read_depth()` 设置。 |
| `VertexBufferRead` | `VertexBuffer` | 状态映射已存在，当前 graph 不管理 buffer handle。 |
| `IndexBufferRead` | `IndexBuffer` | 状态映射已存在，当前 graph 不管理 buffer handle。 |
| `ConstantBufferRead` | `ConstBuffer` | 状态映射已存在，当前 graph 不管理 buffer handle。 |
| `CopySrc` | `CopySrc` | copy source 状态，当前 graph copy pass 尚未封装。 |
| `CopyDst` | `CopyDst` | copy destination 状态，当前 graph copy pass 尚未封装。 |
| `Present` | `Present` | present 状态映射。 |

辅助函数：

```cpp
RHI::AshResourceState render_graph_access_to_rhi_state(RenderGraphAccess access);
const char* render_graph_access_name(RenderGraphAccess access);
```

Depth read mode：

```cpp
enum class RenderGraphDepthReadMode : uint8_t
{
    DepthTestOnly = 0,
    DepthTestAndShaderResource
};
```

`DepthTestAndShaderResource` 会映射为 `DSVRead | SRVGraphics`，用于 lighting pass 既做 read-only depth test 又在 shader 中采样 depth 的场景。

## 7. Pass API

### 7.1 Raster Pass

```cpp
bool add_raster_pass(
    const char* name,
    RenderGraphPassFlags flags,
    const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
    const std::function<bool(RenderGraphRasterContext&)>& execute);
```

setup lambda 立即执行，用于声明资源访问：

```cpp
class RenderGraphRasterPassBuilder
{
public:
    void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
    void write_color(uint8_t slot, RenderGraphTextureRef texture, RenderLoadAction load_action, RenderColorValue clear_color);
    void write_depth(RenderGraphTextureRef texture, RenderLoadAction load_action, RenderDepthStencilValue clear_value);
    void read_depth(RenderGraphTextureRef texture, RenderGraphDepthReadMode mode);
};
```

execute lambda 在 `graph.execute()` 期间执行：

```cpp
class RenderGraphRasterContext
{
public:
    virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
    virtual bool draw(const GraphicsDrawDesc& desc) = 0;
};
```

规则：

- `write_color()` 会成为 `PassDesc::color_attachments[slot]`。
- color slot 建议从 `0` 开始连续声明，避免构造出空 attachment slot。
- `write_depth()` 会成为 writable depth attachment。
- `read_depth()` 会成为 read-only depth attachment，并设置 depth final state。
- `read_texture()` 只声明 SRV/UAV/copy 等非 attachment 依赖，真实 shader binding 仍在 execute lambda 中通过 `context.get_texture()` 后调用 `GraphicsProgram::set_texture()`。
- raster execute 内只提交 draw，不要调用 compute dispatch。

### 7.2 Compute Pass

```cpp
bool add_compute_pass(
    const char* name,
    RenderGraphPassFlags flags,
    const std::function<void(RenderGraphComputePassBuilder&)>& setup,
    const std::function<bool(RenderGraphComputeContext&)>& execute);
```

setup lambda：

```cpp
class RenderGraphComputePassBuilder
{
public:
    void read_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
    void write_texture(RenderGraphTextureRef texture, RenderGraphAccess access);
};
```

execute lambda：

```cpp
class RenderGraphComputeContext
{
public:
    virtual std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) = 0;
    virtual bool dispatch(const ComputeDispatchDesc& desc) = 0;
};
```

典型 compute pass：

```cpp
ASH_PROCESS_ERROR(graph.add_compute_pass(
    "BuildLightTiles",
    RenderGraphPassFlags::None,
    [&](RenderGraphComputePassBuilder& pass)
    {
        pass.read_texture(depth, RenderGraphAccess::ComputeSRV);
        pass.write_texture(tile_mask, RenderGraphAccess::ComputeUAV);
    },
    [&](RenderGraphComputeContext& context) -> bool
    {
        ASH_PROFILE_SCOPE_NC("BuildLightTiles", AshEngine::Profile::Color::Dispatch);
        ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
        std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
        std::shared_ptr<RenderTarget> tile_target = context.get_texture(tile_mask);
        ASH_PROCESS_ERROR(depth_target && tile_target);
        ASH_PROCESS_ERROR(tile_program->set_texture("SceneDepth", depth_target));
        ASH_PROCESS_ERROR(tile_program->set_rw_texture("TileMask", tile_target));

        ComputeDispatchDesc desc{};
        desc.program = tile_program;
        desc.group_count_x = group_count_x;
        desc.group_count_y = group_count_y;
        desc.group_count_z = 1;
        ASH_PROCESS_ERROR(context.dispatch(desc));
        ASH_PROCESS_GUARD_RETURN_END(bResult, false);
    }));
```

## 8. Pass Flags

```cpp
enum class RenderGraphPassFlags : uint8_t
{
    None = 0,
    Raster = 1 << 0,
    Compute = 1 << 1,
    NeverCull = 1 << 2
};
```

使用规则：

- 调用 `add_raster_pass()` 时会自动补 `Raster`。
- 调用 `add_compute_pass()` 时会自动补 `Compute`。
- 普通 pass 使用 `RenderGraphPassFlags::None`。
- 有副作用但不写 graph root 的 pass 使用 `NeverCull`，例如 timestamp、debug marker、side-effect compute。
- 无资源访问且没有 `NeverCull` 的 pass 会被 compiler 判为非法。

## 9. Compiler 规则

`RenderGraphCompiler::compile()` 当前规则：

- pass 按声明顺序分析，live pass 也按声明顺序执行。
- transient texture 必须先由前面的 pass 写入，后面的 pass 才能读取。
- 读取未生产的 transient texture 会编译失败。
- 读取 external texture 合法，因为 external texture 由 graph 外提供。
- 写入 external texture 或 extracted texture 的 producer pass 是 culling root。
- `NeverCull` pass 是 culling root。
- 没有 root 依赖链的 pass 会被剔除，并写日志。
- live texture 会记录 `first_pass` / `last_pass`。
- live pass 会生成 `RenderGraphPassBarrierPlan`，用于测试、诊断和后续扩展。

注意：

- 当前 compiler 不做 DAG 重排。生产者必须声明在消费者之前。
- setup lambda 必须完整声明 execute lambda 会使用的 graph texture，否则 culling 和 barrier plan 都不可信。
- execute lambda 返回 `false` 会让 graph 执行失败，并释放已分配 transient。

## 10. Scene Deferred 示例链

当前默认静态网格 scene path 已通过 graph 表达：

```text
SceneGBufferPass -> SceneDeferredLightingAccumPass -> SceneDeferredCompositePass
```

资源：

- `SceneOutput`：external output target。
- `GBufferA..E`：graph transient MRT，shader_resource=true。
- `SceneDeferredDepth`：graph transient D32 depth，shader_resource=true。
- `SceneDeferredLightingAccum`：graph transient RGBA16F lighting accumulation。

pass 关系：

```cpp
// 1. GBuffer
pass.write_color(0, gbuffer_a, RenderLoadAction::Clear, {});
pass.write_color(1, gbuffer_b, RenderLoadAction::Clear, {});
pass.write_depth(depth, RenderLoadAction::Clear, view_context.depth_clear_value);

// 2. Lighting
pass.read_texture(gbuffer_a, RenderGraphAccess::GraphicsSRV);
pass.read_depth(depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
pass.write_color(0, lighting_accum, RenderLoadAction::Clear, k_lighting_accum_clear_color);

// 3. Composite
pass.read_texture(lighting_accum, RenderGraphAccess::GraphicsSRV);
pass.write_color(0, output, view_context.color_load_action, view_context.color_clear_value);
```

真实 draw 逻辑仍复用 `SceneRenderer`、`DeferredLightingPass`、`GraphicsProgram`、material proxy 和 instance buffer 路径。Graph 只接管 pass/resource 声明和执行外壳。

## 11. Tracy 与命名要求

新增 graph 代码时至少保证：

- graph 名称可读，例如 `"SceneRenderGraph"`、`"ShadowGraph"`。
- pass 名称稳定，能对应 RenderDoc marker、日志和 Tracy zone。
- pass execute lambda 或其调用的具体函数中有 `ASH_PROFILE_SCOPE_NC()`。
- 批量操作用 `ASH_PROFILE_SCOPE_VALUE()` 记录数量，例如 draw count、light count、texture count。
- program、pipeline、material、pass 这类可读名称用 `ASH_PROFILE_SCOPE_TEXT()` 附加。
- 不要在公共头 include Tracy 头；只在 `.cpp` include `Base/hprofiler.h`。

示例：

```cpp
ASH_PROFILE_SCOPE_NC("SceneDeferredLightingAccumPass", AshEngine::Profile::Color::Draw);
ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(frame.lights.size()));
```

## 12. 错误处理与验证

pass 添加和执行都应接入项目现有 process-error 风格：

```cpp
ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
ASH_PROCESS_ERROR(graph.add_raster_pass(...));
ASH_PROCESS_ERROR(graph.execute());
ASH_PROCESS_GUARD_RETURN_END(bResult, false);
```

新增或修改 graph pass 后至少检查：

- `Sandbox.exe --engine-self-test` 中 RenderGraph self-test 通过。
- Debug x64 构建通过。
- 运行路径覆盖 Vulkan 和 DX12，尤其是 depth read-only、SRV sampling、UAV 和 present/output target 相关改动。
- 日志没有 RenderGraph compiler error、RHI command-buffer error、Vulkan validation error、DX12 debug layer error 或 shutdown leak。

## 13. 常见错误

### 13.1 在 execute 中使用未声明的 texture

错误：

```cpp
std::shared_ptr<RenderTarget> lighting = context.get_texture(lighting_ref);
program->set_texture("Lighting", lighting);
```

但 setup 中没有：

```cpp
pass.read_texture(lighting_ref, RenderGraphAccess::GraphicsSRV);
```

结果是 compiler 无法知道依赖，pass 可能被剔除或缺少正确状态计划。

### 13.2 读取未来才写入的 transient

错误：

```text
PassA reads Temp
PassB writes Temp
```

当前 compiler 不做重排，这会失败。应改为：

```text
PassB writes Temp
PassA reads Temp
```

### 13.3 用 transient 当跨 frame 输出

错误：

```cpp
RenderGraphTextureRef history = graph.create_texture(desc, "History");
graph.extract_texture(history);
```

然后期望下一帧继续使用真实 `RenderTarget`。

当前 `extract_texture()` 只保活 graph chain，不持久化资源。跨 frame history 应由外部系统创建 `RenderTarget`，再通过 `register_external_texture()` 引入 graph。

### 13.4 忘记 NeverCull

只有副作用、不写 external 或 extracted texture 的 pass 会被剔除。需要显式标记：

```cpp
graph.add_compute_pass(
    "DebugReadbackKick",
    RenderGraphPassFlags::NeverCull,
    setup,
    execute);
```

### 13.5 depth attachment 格式不匹配

`write_depth()` / `read_depth()` 只能绑定 depth format texture。把 color format texture 当 depth attachment 会在 executor 中报错。

同理，`write_color()` 不能绑定 depth format texture。

## 14. API 分层

稳定使用面：

- `RenderGraphBuilder`
- `RenderGraphTextureRef`
- `RenderGraphTextureDesc`
- `RenderGraphAccess`
- `RenderGraphDepthReadMode`
- `RenderGraphPassFlags`
- `RenderGraphRasterPassBuilder`
- `RenderGraphComputePassBuilder`
- `RenderGraphRasterContext`
- `RenderGraphComputeContext`

测试 / 内部面：

- `RenderGraphBuilder::create_headless_for_tests()`
- `register_external_texture_desc_for_tests()`
- `compile_for_tests()`
- `get_texture_count_for_tests()`
- `get_pass_count_for_tests()`
- `get_textures_for_tests()`
- `get_passes_for_tests()`
- `RenderGraphCompiler`
- `RenderGraphCompileResult`

测试 / 内部 API 可以用于 self-test 和诊断，不应成为 gameplay、Editor 或长期业务代码依赖。

## 15. 新 pass 接入 Checklist

1. pass 放在 Engine Function/Render 合适模块中，不越过 Engine / Editor 边界。
2. graph setup lambda 声明全部 texture read/write。
3. graph execute lambda 不保存 `context.get_texture()` 返回值到 graph 外。
4. producer 声明在 consumer 之前。
5. 有副作用但不写 root 的 pass 标记 `NeverCull`。
6. pass 名称稳定、可读、和日志 / RenderDoc / Tracy 对齐。
7. `.cpp` 中补 `Base/hprofiler.h` 打点。
8. 使用 `ASH_PROCESS_ERROR` 系列处理失败路径。
9. Vulkan / DX12 都能覆盖到相关资源状态。
10. 更新 `README.md` 和相关 `docs/` 文档。
