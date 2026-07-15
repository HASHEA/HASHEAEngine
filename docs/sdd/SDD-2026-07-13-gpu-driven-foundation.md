# SDD-2026-07-13-gpu-driven-foundation: RenderGraph buffer 与 GPU-driven instance 底座（S2 / Phase 1）

## Status

Done（2026-07-15；Phase 1 通用底座与全部自动门禁完成）

## Context

GPU grass、GPU tree 和未来统一 GPU Scene 都需要同一条 compute → visible list → indexed indirect 流程。现有底座只完成了一半：

- RHI 已有 Vulkan/DX12 等价的 draw/draw-indexed/dispatch indirect。
- `StorageBufferDesc::indirect_args`、`AshResourceState::IndirectArgs` 和显式 barrier 已存在。
- Function `GraphicsDrawDesc` 只支持单条 non-indexed indirect；粒子是唯一生产消费者。
- RenderGraph 的 `RenderGraphAccess` 虽含部分 buffer state 名称，但 graph 没有 buffer ref/node/usage、依赖、lifetime、transient 分配或执行 barrier。
- static mesh instance data 是 `SceneRenderer` 私有的 per-frame CPU vertex-buffer ring，不是 resident GPU page contract。

植被若继续沿粒子方式自持多个 buffer 并依赖 program binding 隐式转换，会把 barrier、lifetime 和 draw contract 固化为植被私有实现。Phase 1 只补最小通用底座，不实现生产植被、HZB 或统一 static-mesh renderer。

## Goals

- 让 RenderGraph 把 `StorageBuffer` 作为一等 external/transient resource 管理。
- compiler 对 buffer 执行 producer/consumer 校验、pass culling、lifetime、compile-cache key 和 barrier plan。
- executor 在 active render pass 之外提交 buffer barrier，并提供 context `get_buffer()`。
- Function `GraphicsDrawDesc` 显式支持 non-indexed/indexed indirect、draw count 和 stride。
- 定义版本化的 Prototype、InstancePage、PageHandle、View 与 DrawGroup 核心契约。
- instance page 支持 compact TRS 与 affine 3×4 两种 transform encoding，为植被与未来普通静态网格保留无重写合流路径。
- 通过双后端 compute write → graph buffer transition → indexed indirect draw 全链自测验证。
- 未使用 graph buffer/GPU-driven runtime 的默认场景没有新增 pass、resource 或 per-frame allocation。

## Non-goals

- 不实现 vegetation asset、brush、baker、streaming、grass/tree renderer、wind、HZB 或 HLOD。
- 不迁移现有 `SceneRenderer::render_static_meshes_to_pass`。
- 不把 vertex/index/constant buffer 全部纳入 RenderGraph；Phase 1 只管理 `StorageBuffer`。
- 不实现 transient buffer aliasing、async compute、多 queue 或跨 queue semaphore。
- 不新增 indirect-count、bindless、mesh shader 或 backend-specific fast path。
- 不新增 `SurfaceGPUDriven` material family；该 family 随首个生产 consumer 进入 GPU grass 阶段。
- 不修改或 bless性能/渲染基线。

## Current implementation

- Entry points:
  - graph API：`RenderGraphFwd/Resource/Pass/Builder/Compiler/Executor`。
  - draw API：`Renderer.h::GraphicsDrawDesc` → `Renderer.cpp` → `RenderDevice::draw_indirect`。
  - RHI：`CommandBuffer::cmd_draw_indexed_indirect` 已实现于 Vulkan/DX12。
  - self-test：`RHIIndirectSelfTest` 直接覆盖 compute 写 args → RHI barrier → 三类 indirect API。
- Modules: Function/RenderGraph、Function/Render、Graphics（本阶段原则上复用，不扩 RHI virtual API）、Base self-tests/Sandbox self-test。
- Data flow: graph 只编译 texture topology；execute 时 program binding 自动收集 storage SRV/UAV barrier，indirect args 另由 Renderer 显式收集。
- Known constraints:
  - compiler `pass_barriers` 目前主要用于测试/诊断；executor 不消费 texture transition plan。
  - resource transition 必须发生在 active render pass 之外。
  - external resource 是 culling root；transient 只有 live resource 才分配，graph 结束统一释放。
  - graph topology cache 容量 64，key 必须包含新增 buffer topology/usage。
  - existing particle args 使用 `AshDrawIndirectArgs`，`firstInstance` 恒为 0。

## Proposal

### RenderGraph buffer surface

新增独立类型，不把 texture/buffer index 混用：

```cpp
struct RenderGraphBufferRef { uint32_t index = UINT32_MAX; };

struct RenderGraphBufferDesc
{
    uint32_t size = 0;
    uint32_t stride = 0;
    bool shader_resource = true;
    bool unordered_access = false;
    bool indirect_args = false;
};
```

稳定 public surface：

```cpp
RenderGraphBufferRef register_external_buffer(
    const std::shared_ptr<StorageBuffer>& buffer,
    const char* name,
    RenderGraphAccess initial_access = RenderGraphAccess::Unknown);

RenderGraphBufferRef create_buffer(const RenderGraphBufferDesc& desc, const char* name);
void extract_buffer(RenderGraphBufferRef buffer);

pass.read_buffer(ref, access);
pass.write_buffer(ref, access);
context.get_buffer(ref);
```

`RenderGraphAccess` 新增 `IndirectArgs`。Phase 1 允许的 buffer access 为 `GraphicsSRV`、`ComputeSRV`、`GraphicsUAV`、`ComputeUAV`、`IndirectArgs`；attachment 和 Copy access 对 buffer 非法。`write_buffer` 只接受 UAV，`read_buffer` 只接受 SRV/IndirectArgs。copy pass 与 CopySrc/CopyDst buffer 支持留到出现真实调用点后扩展。非法组合在 setup/compile 阶段明确失败。

### Dependency, culling and lifetime

buffer 与 texture 遵守相同声明顺序规则：

- transient read-before-write 失败；external read 合法；
- 写 external/extracted buffer 的 pass 是 culling root；
- `NeverCull` 语义不变；
- LoadAction 只属于 attachment，不适用于 buffer；
- compiler 输出独立 `buffer_lifetimes`、buffer transitions 和 per-pass states；
- compile cache hash 包含 buffer name/external/extracted/desc 和 pass buffer usages。

本阶段不统一 `RenderGraphTextureRef`/`BufferRef` 为 variant，保持类型安全并控制 diff。

### Allocation and barrier execution

Renderer 增加 transient storage-buffer pool，对 `RenderGraphBufferDesc` 转换后的 `StorageBufferDesc` 做 acquire/release。只为 live transient 分配，graph 结束或任一失败路径统一释放；本阶段不做 aliasing。

external buffer lifetime 仍归调用方。executor 在每个 pass 开始前，根据 compiled buffer plan 构建 RHI barrier 并调用 `Renderer::submit_graph_resource_barriers()`；提交发生在 `begin_pass()` 或 compute dispatch 之前。graph buffer 已声明的状态不再依赖 execute lambda 的调用顺序猜测。

program binding 仍负责未纳入 graph 的资源和 texture 实际 transition。每个 graph context 持有本 pass 的 declared buffer access map；`draw()` / `dispatch()` 在提交前让 RenderDevice 对 program 中绑定的 graph-owned buffer 做身份与状态核对。未声明的 graph-owned buffer、声明为 SRV 却按 UAV 绑定等冲突使 pass 失败，并记录 resource/pass/binding 名；相同目标状态去重，禁止 last-writer-wins。graph 外资源继续走既有 program-binding barrier。

### Function indexed indirect contract

把目前“non-null args buffer 即 non-indexed indirect”的隐式语义替换为显式结构：

```cpp
enum class GraphicsIndirectKind : uint8_t
{
    None = 0,
    NonIndexed,
    Indexed
};

GraphicsIndirectKind indirect_kind = GraphicsIndirectKind::None;
std::shared_ptr<StorageBuffer> indirect_args_buffer;
uint64_t indirect_args_offset = 0;
uint32_t indirect_draw_count = 1;
uint32_t indirect_stride = 0; // 0 = engine struct size for kind
```

规则：

- `None` 要求 args buffer 为空；indirect kind 要求 args buffer 有 `indirect_args` usage。
- `Indexed` 要求有效 index buffer，并先完成 IndexBuffer 与 IndirectArgs transition。
- offset/stride 对齐且整个 args range 在 buffer 内；draw count 必须大于 0。
- indirect 与 direct count/first 字段互斥；不再静默忽略冲突输入。
- args 内 `firstInstance` 必须为 0；GPU instance base 走 draw constants/storage buffer。
- ParticleSystemPass 显式设置 `NonIndexed`，画面与行为不变。

RHI API 不变；RenderDevice 调用既有 `cmd_draw_indirect` 或 `cmd_draw_indexed_indirect`。

### Minimal GPU-driven data contract

新增 `Function/Render/GPUDriven/` 的纯 Function 契约，核心文件为 `GpuDrivenTypes.h` 与 `GpuDrivenPageAllocator.h/.cpp`：

| Type | Contract |
| --- | --- |
| `GpuDrivenPrototypeId` | `uint32_t` stable runtime ID；0 保留 invalid |
| `GpuDrivenPageHandle` | `{slot, generation}`；比较必须含 generation |
| `GpuDrivenTransformEncoding` | `CompressedTRS` / `Affine3x4F32` |
| `GpuDrivenInstancePageDesc` | origin、bounds、encoding、stride、capacity、count |
| `GpuDrivenViewDesc` | camera-relative matrices/frustum、viewport、reverse-Z；HZB 字段后续扩展 |
| `GpuDrivenDrawGroupDesc` | prototype/LOD/section、visible-list base/capacity、args offset |

Phase 1 只实现 CPU page-slot allocator、generation-safe handle、layout validation 与 GPU buffer ownership helper，不实现 streaming policy 或 production culling。

`CompressedTRS` 与 `Affine3x4F32` 是版本化 payload encoding，不强迫所有 instance 使用同一固定 struct。page header 和 draw output 稳定；后续添加 encoding 必须新增版本/permutation，禁止原地改变 stride 语义。

### Self-test

扩展 indirect self-test 或新增聚焦 self-test，构造：

1. external candidate buffer + transient visible/args buffer；
2. RenderGraph compute pass 声明 SRV/UAV，写入一个 indexed indirect args；
3. raster pass 声明 args 为 `IndirectArgs`，通过 Function `GraphicsDrawDesc::Indexed` 绘制；
4. readback 验证已知像素和 args 内容；
5. Vulkan/DX12 Debug/Release、validation 和 WARP/lavapipe smoke 运行同一逻辑。

headless compiler tests覆盖 read-before-write、external root、dead transient culling、lifetime、barrier state、compile cache、invalid access 和 extract buffer。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| RenderGraph types | BufferRef/Desc/Usage/Node/Context | `RenderGraphFwd/Resource/Pass/Builder.*` |
| RenderGraph compiler | buffer dependency/lifetime/barrier/cache | `RenderGraphCompiler.*` |
| RenderGraph executor | transient buffer pool、context lookup、pre-pass barrier | `RenderGraphExecutor.cpp`、`Renderer.*`、`RenderDevice.*` |
| Function draw | explicit indirect kind + indexed path/validation | `Renderer.h/.cpp`、`RenderDevice.h/.cpp` |
| GPU-driven foundation | page/prototype/view/draw-group contracts + allocator | `Function/Render/GPUDriven/`（新） |
| Particle | 显式 NonIndexed 迁移 | `ParticleSystemPass.cpp`、相关 tests |
| Tests/self-test | graph buffer + Function indexed indirect full chain | `project/src/tests/`、`EngineSelfTests.cpp` 或聚焦 self-test 文件 |
| Docs | RenderGraph/render/graphics specs 回写 | `docs/specs/modules/`、indirect contract references |

### API / contract changes

- RenderGraph 新增 buffer public API；属于向后兼容扩展，但 topology hash/schema 改变。
- `GraphicsDrawDesc` indirect 由隐式字段改为显式 kind；仓库内唯一生产调用点 Particle 必须同阶段迁移。
- 新增 Function-level GPU-driven core types；不暴露到 Editor/Sandbox。
- 不新增/修改 Graphics `CommandBuffer` virtual method。

### Backend impact

- RHI indexed indirect 已双后端实现，本阶段主要验证 Function 调用和 graph barrier。
- Vulkan/DX12 的 StorageBuffer UAV→IndirectArgs、UAV→SRV、copy transitions 必须等价。
- validation/debug-layer error 视同 FAIL；禁止以关闭 validation 解决 barrier 问题。
- optional indirect-count/bindless 不在本阶段，不新增 capability gate。

### Performance

- 默认 graph 无 buffer 时，不分配 transient buffer，不执行新增 buffer loop 之外的工作。
- compile cache key 增加 buffer topology；texture-only graph 应继续稳定命中。
- transient pool 禁止每帧反复创建相同 desc 的 GPU buffer。
- Phase 1 完成判据使用 Phase 0 `VegetationFullPipeline` no-content baseline：CPU/GPU 变化不得超过该 profile 已批准阈值；无 baseline 时禁止声称“零回归”。
- self-test 默认关闭，不进入普通帧。

## Verification plan

| Verification | Coverage | Command / evidence |
| --- | --- | --- |
| Unit | buffer graph compiler/cache/lifetime/access；page generation/layout | `RunTests.bat Debug` |
| Engine self-test | Function indexed indirect + graph buffer full chain | 聚焦 CLI，Vulkan/DX12 Debug+Release |
| Build | Function/API and both backends | `build_editor.bat Debug`、`build_sandbox.bat Debug`、`build_sandbox.bat Release` |
| Architecture | new directory/include direction | `RunArchGate.bat` |
| Render regression | particle migration + default scenes | `RunRenderGate.bat` |
| Performance | texture-only graph overhead / compile cache / no-content baseline | `RunPerfGate.bat -Profile Standard` + Phase 0 profile |
| Validation | UAV/SRV/IndirectArgs/lifetime | 双后端 validation 下 self-test 与 Sandbox smoke |
| Tool/plan | dirty-path validation coverage | `scripts/AIDevDoctor.ps1 -Mode ValidatePlan` |

## Task breakdown

1. 先写 headless failing tests：BufferRef access、dependency、culling、lifetime、cache、barrier。
2. 实现 graph buffer node/compiler，不接 executor；单测通过。
3. 实现 transient storage pool、context 和 pre-pass barrier；CPU/fake tests 通过。
4. 先写 Function indexed indirect validation tests，再实现 Renderer/RenderDevice path。
5. 迁移 Particle 为 explicit NonIndexed，既有粒子 RenderGate 不变。
6. 定义最小 GPU-driven contracts/page allocator，补 generation/layout tests。
7. 双后端全链 self-test、validation、RenderGate 和 Phase 0 Perf profile。
8. 回写 render-graph/render/graphics specs；未进入生产的类型明确标 experimental foundation。

## Risks

| Risk | Mitigation |
| --- | --- |
| graph 声明与 program binding 对同 buffer 给出冲突状态 | pre-pass 合并去重并检测冲突；错误包含 pass/resource |
| transient buffer 失败路径泄漏 | texture 同款 scope cleanup；异常/false 路径统一 release |
| compile cache 误命中旧 topology | buffer desc/usage/external/extracted 全量入 hash；专门 collision/regression tests |
| indirect kind 迁移破坏粒子 | 唯一生产调用点同阶段显式迁移；particles golden 双后端锁定 |
| CompressedTRS 不能表达未来 static mesh shear | 同一 page contract 支持 Affine3x4F32，不强迫有损迁移 |
| 提前做完整 GPU Scene 导致范围膨胀 | Phase 1 禁止 HZB/streaming/shader/scene migration，只交付可测试 foundation |
| buffer barrier 进入 active render pass | executor 只在 begin_pass/dispatch 前提交；validation 作为硬门禁 |

## Open questions

- 无实现阻塞项。Phase 0 产出的 approved performance thresholds 已作为本阶段最终性能比较输入。
- `GpuDrivenInstancePage` 的具体 compact payload bit allocation 延后到 GPU grass SDD；Phase 1 只冻结 encoding/version/stride contract，不猜测最终量化精度。

## Implementation outcome

- RenderGraph 已把 external/transient `StorageBuffer` 纳入 public API、dependency/culling/lifetime/cache、pre-pass barrier 与 program-binding identity/access validation；texture-only/default scene 不创建新增 graph buffer pass/resource/allocation。
- Function draw 已使用显式 `None/NonIndexed/Indexed` indirect contract；Particle 迁移为 explicit non-indexed。既有 Vulkan/DX12 RHI virtual surface 未扩张。
- `Function/Render/GPUDriven/` 已提供 prototype/page/view/draw-group、generation-safe page retirement、layout validation 与 storage ownership helper；它是后续 grass/tree 与全局 GPUDriven 合流的通用底座，不包含生产植被、刷子、流送、GPU culling、HZB、HLOD 或 SpeedTree 资产入口。
- raw RHI 与 Function RenderGraph bounded full-chain self-test 在 Vulkan/DX12 Debug/Release 均 PASS；Debug 双后端确认 validation 启用。Release 构建按既有策略没有 Vulkan/DX12 validation 能力，因此 Release 结果只作为功能证据，没有虚报 validation。
- 最终 CPU 证据为 focused 19/19 cases、330/330 assertions；full 176/176 cases、2510/2510 assertions；ArchGate PASS（35 条既有 legacy WARN）；Debug Editor/Sandbox 与 Release Sandbox 构建 PASS，AIDevDoctor PASS。
- readiness、双后端 Debug validation/self-test、RenderGate 与性能门禁全部通过。RenderGate 报告 `20260715-123227-061-46660-b5d80489` 未 bless；sandbox Vulkan/DX12/cross SSIM 为 `0.996278 / 0.996177 / 0.999747`，particles 三项为 `1.0`。
- 最终性能工具提交为 `0ef8da1efa24bd85107681047907d7790c59c4a0`。同一 exact HEAD 的 Standard 报告 `20260715-151351-919-17136-479faf75` 四组合 PASS；最终有效 VegetationFullPipeline Release non-bless 报告 `20260715-153105-257-3488-3a9c623c` 双后端 PASS/COMPARED、11/11 metrics、总/逐项 GPU coverage=1、warnings/failures=0。中间 WARN 与窗口最小化造成的 coverage FAIL 均按 stop-rule 排除；未 import/bless，baseline 保持批准 SHA-256 `49D3FCCB0C068D0A90E5D2BAE667A5FDA3EB6476E6B444885F0DC103716A4659`。
- 本阶段不声称达到最终“2K 300 FPS 百万植被”产品目标；当前 VFP 是 no-content foundation baseline。生产草/树、分块流送、GPU culling、HLOD/远景替代和地形笔刷接线必须进入后续独立 S2 设计。
