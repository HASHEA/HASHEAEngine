---
owner: huyizhou
last_reviewed: 2026-07-15
status: active
---

# Module Spec: RenderGraph（API 契约）

> 由 `docs/RenderGraphAPISpec.md` 迁入并按当前代码核实更新。

## 职责与边界

Function/Render 层的帧级声明式 pass 编排系统：声明 frame-local texture 与 `StorageBuffer` 资源图及 raster/compute pass 的读写关系，编译 live pass、transient lifetime 与 barrier plan，经 `Renderer`/`RenderDevice` 执行。不是新的 RHI backend，不向 Editor/业务层暴露 Vulkan/DX12 细节。**不负责**：vertex/index/constant buffer、sampler/shader/pipeline/material 生命周期，shader parameter block 自动生成，buffer copy pass，跨帧保存 transient，async compute、pass 重排、transient aliasing或跨 queue 同步。

## 目录与关键文件

| 路径（`project/src/engine/Function/Render/`） | 内容 |
| --- | --- |
| `RenderGraph.h` | 聚合头（普通使用者 include 这个） |
| `RenderGraphFwd.h` | 前置声明 + `RenderGraphTextureRef` / `RenderGraphBufferRef` |
| `RenderGraphResource.h/.cpp` | `RenderGraphAccess`、texture/buffer desc、access→RHI 状态映射 |
| `RenderGraphPass.h/.cpp` | `RenderGraphPassFlags`、pass node、raster/compute PassBuilder 与 Context |
| `RenderGraphBuilder.h/.cpp` | `RenderGraphBuilder`：资源注册、pass 添加、execute |
| `RenderGraphCompiler.h/.cpp` | culling、lifetime、barrier plan 编译 + 拓扑级 compile cache |
| `RenderGraphExecutor.cpp` | `execute_render_graph()`：live transient texture/buffer 分配、pre-pass barrier、binding scope、pass 循环与统一 cleanup |

## 公共接口

### 基本工作流

一次 graph 是 frame/view 级临时对象：

```cpp
RenderGraphBuilder graph(*m_renderer, "SceneRenderGraph");
RenderGraphTextureRef output = graph.register_external_texture(view_context.output_target, "SceneOutput");
RenderGraphTextureRef lighting = graph.create_texture(color_desc, "Lighting");
graph.add_raster_pass("LightingPass", RenderGraphPassFlags::None,
    RHI::GpuTimingMetric::DeferredLighting,
    [&](RenderGraphRasterPassBuilder& pass) { pass.write_color(0, lighting, RenderLoadAction::Clear, {}); },
    [&](RenderGraphRasterContext& context) -> bool { return context.draw(draw_desc); });
graph.execute();
```

setup lambda 在 `add_*_pass()` 时立即执行，声明全部资源访问；execute lambda 在 `graph.execute()` 期间执行，经 context 取真实 `RenderTarget` 并提交 draw/dispatch。

### Resource refs

`RenderGraphTextureRef` 与 `RenderGraphBufferRef` 都是 `{ uint32_t index }` + `is_valid()` / `operator bool` / `==` / `!=`。只在创建它们的 builder 内有效；不得跨 frame/view/graph 缓存。`context.get_texture(ref)` / `get_buffer(ref)` 返回的真实资源只应在当前 execute lambda 内使用。

### External / Transient / Extract

```cpp
RenderGraphTextureRef register_external_texture(const std::shared_ptr<RenderTarget>& texture, const char* name,
    RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
void extract_texture(RenderGraphTextureRef texture);
RenderGraphBufferRef register_external_buffer(const std::shared_ptr<StorageBuffer>& buffer, const char* name,
    RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
RenderGraphBufferRef create_buffer(const RenderGraphBufferDesc& desc, const char* name);
void extract_buffer(RenderGraphBufferRef buffer);
```

- external：把 graph 外拥有的 `RenderTarget` / `StorageBuffer` 引入 graph，是 culling root；desc 从真实资源推导，传空指针记错误并返回 invalid ref，lifetime 仍归外部。texture 的 `initial_access` 保持既有预留语义；buffer 的 initial access 参与首个 live usage 的状态计划。
- transient：texture 经 `Renderer::acquire_transient_render_target()`，buffer 经 `acquire_transient_storage_buffer()`；仅 live（culling 后仍被使用）的 transient 才分配。graph 正常结束及所有 allocation/barrier/pass 失败路径都会统一 release；当前不做 aliasing 或 pass 后立即释放。
- extract：仅把 transient texture/buffer 标为 culling root以保留 producer 链；**不**返回可跨帧持久保存的资源。跨帧输出必须由外部创建并注册 external。

### RenderGraphTextureDesc

`width`/`height`（`uint16_t`，调用方先检查范围）、`format`（`RenderTextureFormat`，depth 用 `D24_UNORM_S8_UINT` 或 `D32_SFLOAT`）、`shader_resource`（默认 true）、`unordered_access`、`use_optimized_clear_value` + `optimized_clear_color`/`optimized_clear_depth_stencil`；与 `RenderTargetDesc` 互转：`from_render_target_desc()` / `to_render_target_desc(name)`。

### RenderGraphBufferDesc

`size` / `stride`、`shader_resource`、`unordered_access`、`indirect_args`；与 `StorageBufferDesc` 互转：`from_storage_buffer_desc()` / `to_storage_buffer_desc(name)`。Phase 1 只允许 `StorageBuffer`，不把 vertex/index/constant buffer 纳入 graph。

### RenderGraphAccess

| Access | RHI 状态 | 用途 |
| --- | --- | --- |
| `Unknown` | `Unknown` | 保留，不要在 pass 声明中主动使用 |
| `GraphicsSRV` / `ComputeSRV` | `SRVGraphics` / `SRVCompute` | raster / compute 的 texture 或 storage-buffer SRV 读取 |
| `GraphicsUAV` / `ComputeUAV` | `UAVGraphics` / `UAVCompute` | texture 或 storage-buffer UAV 写；graphics UAV 慎用 |
| `ColorAttachmentWrite` | `RTV` | 由 `write_color()` 设置 |
| `DepthStencilWrite` | `DSVWrite` | 由 `write_depth()` 设置 |
| `DepthStencilRead` | `DSVRead` 或 `DSVRead\|SRVGraphics` | 由 `read_depth()` 设置，按 `RenderGraphDepthReadMode` |
| `IndirectArgs` | `IndirectArgs` | raster pass 读取带 `indirect_args` usage 的 storage buffer |
| `VertexBufferRead` / `IndexBufferRead` / `ConstantBufferRead` | 对应 buffer 状态 | 映射存在，但 Phase 1 graph buffer API 明确拒绝 |
| `CopySrc` / `CopyDst` | copy 状态 | Phase 1 graph buffer API 明确拒绝；尚无 buffer copy pass |
| `Present` | `Present` | present 状态映射 |

storage buffer 的合法 access 只有 `GraphicsSRV`、`ComputeSRV`、`GraphicsUAV`、`ComputeUAV`、`IndirectArgs`；`read_buffer()` 只接受 SRV/IndirectArgs，`write_buffer()` 只接受 UAV，并同时核对 desc capability。辅助函数：`render_graph_access_to_rhi_state(access)`、`render_graph_depth_read_state(mode)`、`render_graph_access_name(access)`。

### Pass API

```cpp
bool add_raster_pass(const char* name, RenderGraphPassFlags flags,
    RHI::GpuTimingMetric timing_metric,
    const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
    const std::function<bool(RenderGraphRasterContext&)>& execute);
bool add_compute_pass(const char* name, RenderGraphPassFlags flags,
    RHI::GpuTimingMetric timing_metric,
    const std::function<void(RenderGraphComputePassBuilder&)>& setup,
    const std::function<bool(RenderGraphComputeContext&)>& execute);
```

Raster setup：`read_texture(ref, access)`、`read_buffer(ref, access)`、`write_buffer(ref, access)`、`write_color(...)`、`write_depth(...)`、`read_depth(...)`。Compute setup：`read_texture()`、`write_texture()`、`read_buffer()`、`write_buffer()`。
Context：`get_texture(ref)`、`get_buffer(ref)` + raster `draw(GraphicsDrawDesc)` / compute `dispatch(ComputeDispatchDesc)`。

规则：

- `write_color()` 成为 `PassDesc::color_attachments[slot]`；slot 从 0 连续声明，避免空 attachment slot。`write_depth()` / `read_depth()` 成为 writable / read-only depth attachment，read_depth 同时设置 depth final state。
- `read_texture()` 只声明依赖；真实 shader binding 仍在 execute lambda 中 `get_texture()` 后调 `GraphicsProgram::set_texture()` 等。
- graph buffer 声明与 program 实际绑定必须精确一致：executor 为当前 pass 建立 non-owning binding scope，资源身份、SRV/UAV/IndirectArgs 状态或未声明使用发生冲突时 fail-closed；args buffer 可先作为 `IndirectArgs` 供 draw，再在后续 pass 以 `GraphicsSRV` 验证。
- raster execute 内只提交 draw，不要调 compute dispatch。
- attachment 格式校验在 executor：depth 格式当 color attachment（或反之）报错并使 graph 执行失败。
- `timing_metric` 必须是 `GPU.Frame` 之外的稳定 pass-group metric；`Invalid` 表示显式不计入结构化 GPU timing，`Frame`/`Count` 被拒绝。executor 只遍历 live pass，并把相邻且 metric 相同的 pass 合并为一个 scope；metric 切换、untracked pass、execute 失败和 graph 结束都必须先闭合当前 scope。metric 是 compile-cache topology identity 的一部分，禁止同一 cache key 复用不同 timing 分组。

### RenderGraphPassFlags

`None / Raster / Compute / NeverCull`。`add_raster_pass()` 自动补 `Raster`，`add_compute_pass()` 自动补 `Compute`。有副作用但不写 culling root 的 pass（timestamp、debug marker、side-effect compute）用 `NeverCull`。无任何资源访问且没有 `NeverCull` 的 pass 使整个 compile 失败。

### Compiler 规则

`RenderGraphCompiler::compile()`：

- pass 按声明顺序分析，live pass 也按声明顺序执行；**不做 DAG 重排**，producer 必须声明在 consumer 之前。
- 读取未生产的 transient → 编译失败；读取 external 合法。
- 以 `RenderLoadAction::Load` 写 color/depth attachment 视作对已有内容的读，参与依赖链（会保留此前的 producer）。
- culling root：写 external / extracted texture 或 buffer 的 producer pass、`NeverCull` pass；无 root 依赖链的 pass 被剔除并写日志。
- 产出 `RenderGraphCompileResult`：`live_pass_indices`、texture/buffer lifetimes（`first_pass`/`last_pass`）、每 pass texture barrier plan 与 ordered buffer transitions。executor 在 begin raster pass 或 compute dispatch 前、active render pass 之外提交 graph buffer barrier；program binding 收集仍在同一 pre-pass 阶段，并与 graph binding scope做身份/状态合并校验。
- **compile cache**：executor 走 `compile_cached()`；cache identity 精确包含 texture 与 buffer 名称、desc、external/extracted、initial access、pass kind/flags/timing metric及全部 texture/buffer usages。线程安全，容量 64 条超限清空；任何 buffer topology/access 差异不得复用旧结果。

setup 必须完整声明 execute 会用到的 graph texture/buffer，否则 culling、barrier 与 binding validation 不可信。execute lambda 返回 `false` 或 allocation/barrier/begin/dispatch/draw 任一失败都会使 graph 执行失败，并释放本次已取得的全部 transient。

### API 分层

- 稳定使用面：`RenderGraphBuilder`、texture/buffer ref 与 desc、`RenderGraphAccess`、`RenderGraphDepthReadMode`、`RenderGraphPassFlags`、两类 PassBuilder 与 Context。
- 测试/内部面（self-test 与诊断用，不得成为业务依赖）：`create_headless_for_tests()`、texture/buffer desc-only external 注册、`compile_for_tests()` / `compile_cached_for_tests()`、texture/buffer/pass introspection、`RenderGraphCompiler` cache stats/reset、`RenderGraphCompileResult`。

场景主链（GBuffer → … → tone-map）如何用本 API 组织见 [render.md](render.md)。

## 约束与不变式

- 只在 Engine Function/Render 层使用；不从 Editor/业务层 include 后端 RHI 头补 graph 能力；不把 Tracy 头或 backend 类型扩散到公共头（`.cpp` 用 `Base/hprofiler.h`）。
- 所有 resource transition 在 active render pass 之外提交（Vulkan/DX12 共享路径合法性的硬约束）；pass execute 内不得注入 backend barrier。
- pass/graph 名称稳定可读，与日志、RenderDoc marker、Tracy zone 对齐；新增/修改 graph pass 必须补 `ASH_PROFILE_SCOPE_NC()` 打点，批量操作用 `ASH_PROFILE_SCOPE_VALUE()`。
- 失败路径走 `ASH_PROCESS_ERROR` / `ASH_PROCESS_GUARD_RETURN` 系列。

常见错误（均已在 compiler/executor 有对应报错）：execute 使用 setup 未声明的 texture/buffer；读取靠后 pass 才写入的 transient；把 `extract_*()` 当跨帧持久化；buffer access 与 desc capability 不符；同一 pass 给同一 buffer 冲突状态；program 绑定了另一个同名/同槽资源；纯副作用 pass 忘记 `NeverCull`；depth/color attachment 格式错配。

## 验证

对齐 `docs/VERIFY.md`「RenderGraph 核心（compile/barrier/lifetime）」行：构建、`RunRenderGate.bat`、PerfGate Standard 与 VegetationFullPipeline；双后端 Debug validation 下运行 `run.bat sandbox <backend> Debug --rhi-selftest-indirect --run-for-frames=1`。必须同时出现 raw RHI 与 Function RenderGraph PASS、clean exit，且无 compiler/command-buffer/validation/debug-layer error 或 shutdown leak。Release 双后端运行相同功能自测，但当前 validation 仅 Debug 可用。

## 历史

- `docs/superpowers/specs/2026-05-14-render-graph-design.md`（初版设计）
- `docs/RenderGraphAPISpec.md`（前身教程式文档，2026-07-04 迁入本文件后删除，见 git 历史）
- [SDD-2026-07-13-gpu-driven-foundation](../../sdd/SDD-2026-07-13-gpu-driven-foundation.md)（一等 `StorageBuffer`、显式 indexed indirect 与 GPU-driven Phase 1 底座）
