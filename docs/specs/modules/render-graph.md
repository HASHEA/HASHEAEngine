---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Module Spec: RenderGraph（API 契约）

> 由 `docs/RenderGraphAPISpec.md` 迁入并按当前代码核实更新。

## 职责与边界

Function/Render 层的帧级声明式 pass 编排系统：声明 frame-local texture 资源图与 raster/compute pass 的读写关系，编译 live pass、transient lifetime 与 barrier plan，经 `Renderer`/`RenderDevice` 执行。不是新的 RHI backend，不向 Editor/业务层暴露 Vulkan/DX12 细节。**不负责**：buffer/sampler/shader/pipeline/material 生命周期、shader parameter block 自动生成、跨帧保存 transient、async compute、pass 重排、transient aliasing、跨 queue 同步。

## 目录与关键文件

| 路径（`project/src/engine/Function/Render/`） | 内容 |
| --- | --- |
| `RenderGraph.h` | 聚合头（普通使用者 include 这个） |
| `RenderGraphFwd.h` | 前置声明 + `RenderGraphTextureRef` |
| `RenderGraphResource.h/.cpp` | `RenderGraphAccess`、`RenderGraphDepthReadMode`、`RenderGraphTextureDesc`、access→RHI 状态映射 |
| `RenderGraphPass.h/.cpp` | `RenderGraphPassFlags`、pass node、raster/compute PassBuilder 与 Context |
| `RenderGraphBuilder.h/.cpp` | `RenderGraphBuilder`：资源注册、pass 添加、execute |
| `RenderGraphCompiler.h/.cpp` | culling、lifetime、barrier plan 编译 + 拓扑级 compile cache |
| `RenderGraphExecutor.cpp` | `execute_render_graph()`：transient 分配、pass 循环、attachment 组装 |

## 公共接口

### 基本工作流

一次 graph 是 frame/view 级临时对象：

```cpp
RenderGraphBuilder graph(*m_renderer, "SceneRenderGraph");
RenderGraphTextureRef output = graph.register_external_texture(view_context.output_target, "SceneOutput");
RenderGraphTextureRef lighting = graph.create_texture(color_desc, "Lighting");
graph.add_raster_pass("LightingPass", RenderGraphPassFlags::None,
    [&](RenderGraphRasterPassBuilder& pass) { pass.write_color(0, lighting, RenderLoadAction::Clear, {}); },
    [&](RenderGraphRasterContext& context) -> bool { return context.draw(draw_desc); });
graph.execute();
```

setup lambda 在 `add_*_pass()` 时立即执行，声明全部资源访问；execute lambda 在 `graph.execute()` 期间执行，经 context 取真实 `RenderTarget` 并提交 draw/dispatch。

### RenderGraphTextureRef

`{ uint32_t index }` + `is_valid()` / `operator bool` / `==` / `!=`。只在创建它的 builder 内有效；不得跨 frame/view/graph 缓存。`context.get_texture(ref)` 返回的 `RenderTarget` 只应在当前 execute lambda 内使用。

### External / Transient / Extract

```cpp
RenderGraphTextureRef register_external_texture(const std::shared_ptr<RenderTarget>& texture, const char* name,
    RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
void extract_texture(RenderGraphTextureRef texture);
```

- external：把 graph 外拥有的 `RenderTarget`（输出目标、history、static cache 等）引入 graph，是 culling root；desc 从 RenderTarget 推导，depth 纹理推导为 `shader_resource=false`；传空指针记错误并返回 invalid ref；`initial_access` 是预留参数，不参与 compiler 推导；lifetime 仍归外部。
- transient：执行时经 `Renderer::acquire_transient_render_target()` 分配，graph 结束（或执行失败）统一 `release_transient_render_target()`；仅 live（culling 后仍被使用）的 transient 才会分配。compiler 计算了 lifetime，但当前不做 aliasing / pass 后立即释放，不要依赖。
- extract：仅把 transient 标为 culling root 以保留 producer 链；**不**返回可持久保存的资源，transient 仍在 `execute()` 后释放。跨 frame 输出必须外部建 `RenderTarget` 走 external。

### RenderGraphTextureDesc

`width`/`height`（`uint16_t`，调用方先检查范围）、`format`（`RenderTextureFormat`，depth 用 `D24_UNORM_S8_UINT` 或 `D32_SFLOAT`）、`shader_resource`（默认 true）、`unordered_access`、`use_optimized_clear_value` + `optimized_clear_color`/`optimized_clear_depth_stencil`；与 `RenderTargetDesc` 互转：`from_render_target_desc()` / `to_render_target_desc(name)`。

### RenderGraphAccess

| Access | RHI 状态 | 用途 |
| --- | --- | --- |
| `Unknown` | `Unknown` | 保留，不要在 pass 声明中主动使用 |
| `GraphicsSRV` / `ComputeSRV` | `SRVGraphics` / `SRVCompute` | raster / compute pass SRV 读取 |
| `GraphicsUAV` / `ComputeUAV` | `UAVGraphics` / `UAVCompute` | UAV 访问；graphics UAV 慎用 |
| `ColorAttachmentWrite` | `RTV` | 由 `write_color()` 设置 |
| `DepthStencilWrite` | `DSVWrite` | 由 `write_depth()` 设置 |
| `DepthStencilRead` | `DSVRead` 或 `DSVRead\|SRVGraphics` | 由 `read_depth()` 设置，按 `RenderGraphDepthReadMode` |
| `VertexBufferRead` / `IndexBufferRead` / `ConstantBufferRead` | 对应 buffer 状态 | 映射已存在；graph 当前不管理 buffer handle |
| `CopySrc` / `CopyDst` | copy 状态 | graph copy pass 尚未封装 |
| `Present` | `Present` | present 状态映射 |

辅助函数：`render_graph_access_to_rhi_state(access)`、`render_graph_depth_read_state(mode)`、`render_graph_access_name(access)`。`RenderGraphDepthReadMode::DepthTestAndShaderResource` 映射 `DSVRead|SRVGraphics`，用于 read-only depth test 同时在 shader 采样 depth 的 pass。

### Pass API

```cpp
bool add_raster_pass(const char* name, RenderGraphPassFlags flags,
    const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
    const std::function<bool(RenderGraphRasterContext&)>& execute);
bool add_compute_pass(const char* name, RenderGraphPassFlags flags,
    const std::function<void(RenderGraphComputePassBuilder&)>& setup,
    const std::function<bool(RenderGraphComputeContext&)>& execute);
```

Raster setup：`read_texture(ref, access)`、`write_color(slot, ref, load_action, clear_color)`、`write_depth(ref, load_action, clear_value)`、`read_depth(ref, mode)`。Compute setup：`read_texture(ref, access)`、`write_texture(ref, access)`。
Context：`get_texture(ref)` + raster `draw(GraphicsDrawDesc)` / compute `dispatch(ComputeDispatchDesc)`。

规则：

- `write_color()` 成为 `PassDesc::color_attachments[slot]`；slot 从 0 连续声明，避免空 attachment slot。`write_depth()` / `read_depth()` 成为 writable / read-only depth attachment，read_depth 同时设置 depth final state。
- `read_texture()` 只声明依赖；真实 shader binding 仍在 execute lambda 中 `get_texture()` 后调 `GraphicsProgram::set_texture()` 等。
- raster execute 内只提交 draw，不要调 compute dispatch。
- attachment 格式校验在 executor：depth 格式当 color attachment（或反之）报错并使 graph 执行失败。

### RenderGraphPassFlags

`None / Raster / Compute / NeverCull`。`add_raster_pass()` 自动补 `Raster`，`add_compute_pass()` 自动补 `Compute`。有副作用但不写 culling root 的 pass（timestamp、debug marker、side-effect compute）用 `NeverCull`。无任何资源访问且没有 `NeverCull` 的 pass 使整个 compile 失败。

### Compiler 规则

`RenderGraphCompiler::compile()`：

- pass 按声明顺序分析，live pass 也按声明顺序执行；**不做 DAG 重排**，producer 必须声明在 consumer 之前。
- 读取未生产的 transient → 编译失败；读取 external 合法。
- 以 `RenderLoadAction::Load` 写 color/depth attachment 视作对已有内容的读，参与依赖链（会保留此前的 producer）。
- culling root：写 external / extracted texture 的 producer pass、`NeverCull` pass；无 root 依赖链的 pass 被剔除并写日志。
- 产出 `RenderGraphCompileResult`：`live_pass_indices`、`texture_lifetimes`（`first_pass`/`last_pass`）、每 pass `RenderGraphPassBarrierPlan`。barrier plan 目前供测试/诊断/后续扩展；executor 实际的 SRV/UAV 状态转换仍由 `Renderer`/`RenderDevice` 在 pass 外按 program 绑定收集提交。
- **compile cache**：executor 走 `RenderGraphCompiler::compile_cached()`，按 graph 拓扑（texture 名/external/extracted + pass 名/kind/flags/usages）哈希缓存编译结果；线程安全，容量 64 条超限清空。拓扑不变的逐帧 graph 命中缓存。

setup 必须完整声明 execute 会用到的 graph texture，否则 culling 与 barrier plan 不可信。execute lambda 返回 `false` 使 graph 执行失败并释放已分配 transient。

### API 分层

- 稳定使用面：`RenderGraphBuilder`、`RenderGraphTextureRef`、`RenderGraphTextureDesc`、`RenderGraphAccess`、`RenderGraphDepthReadMode`、`RenderGraphPassFlags`、`RenderGraphRasterPassBuilder`、`RenderGraphComputePassBuilder`、`RenderGraphRasterContext`、`RenderGraphComputeContext`。
- 测试/内部面（self-test 与诊断用，不得成为业务依赖）：`create_headless_for_tests()`、`register_external_texture_desc_for_tests()`、`compile_for_tests()`、`compile_cached_for_tests()`、`get_texture_count_for_tests()`、`get_pass_count_for_tests()`、`get_textures_for_tests()`、`get_passes_for_tests()`、`RenderGraphCompiler`（含 `reset_compile_cache_for_tests()`、`get_compile_cache_stats_for_tests()`）、`RenderGraphCompileResult`、`RenderGraphCompileCacheStats`。

场景主链（GBuffer → … → tone-map）如何用本 API 组织见 [render.md](render.md)。

## 约束与不变式

- 只在 Engine Function/Render 层使用；不从 Editor/业务层 include 后端 RHI 头补 graph 能力；不把 Tracy 头或 backend 类型扩散到公共头（`.cpp` 用 `Base/hprofiler.h`）。
- 所有 resource transition 在 active render pass 之外提交（Vulkan/DX12 共享路径合法性的硬约束）；pass execute 内不得注入 backend barrier。
- pass/graph 名称稳定可读，与日志、RenderDoc marker、Tracy zone 对齐；新增/修改 graph pass 必须补 `ASH_PROFILE_SCOPE_NC()` 打点，批量操作用 `ASH_PROFILE_SCOPE_VALUE()`。
- 失败路径走 `ASH_PROCESS_ERROR` / `ASH_PROCESS_GUARD_RETURN` 系列。

常见错误（均已在 compiler/executor 有对应报错）：execute 用了 setup 未声明的 texture（可能被剔除或缺状态计划）；读取靠后 pass 才写入的 transient（不重排，直接失败）；用 transient + `extract_texture()` 当跨帧输出（不持久化）；纯副作用 pass 忘记 `NeverCull`；depth/color attachment 格式错配。

## 验证

对齐 `docs/VERIFY.md`「RenderGraph 核心（compile/barrier/lifetime）」行，同 RHI 级别：构建 + `RunRenderGate.bat` + `RunPerfGate.bat -Profile Standard`；Engine.ini 开 `[VulkanValidation]` 与 `[DX12Validation]` 各跑一次 smoke，关注 barrier/lifetime 相关 validation 输出。`Sandbox.exe --engine-self-test` 中 RenderGraph self-test（headless compile、culling、barrier plan、compile cache）必须通过。日志不得有 RenderGraph compiler error、RHI command-buffer error、validation/debug-layer error 或 shutdown leak。

## 历史

- `docs/superpowers/specs/2026-05-14-render-graph-design.md`（初版设计）
- `docs/RenderGraphAPISpec.md`（前身教程式文档，2026-07-04 迁入本文件后删除，见 git 历史）
