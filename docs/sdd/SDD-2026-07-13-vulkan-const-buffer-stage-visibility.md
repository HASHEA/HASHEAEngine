# SDD-2026-07-13-vulkan-const-buffer-stage-visibility: Vulkan 常量缓冲可见阶段修正

## Status

Done

## Context

`AshResourceState::ConstBuffer` 是 Graphics 公共资源状态。Function 层对 graphics program 与 compute program 的 uniform buffer 都使用该状态；Material V2 已经在 graphics program 中绑定真实 uniform buffer，`ComputeProgram::set_uniform_buffer()` 也是公开可用接口。

Vulkan 后端当前把 `ConstBuffer` 的 pipeline stage 固定为 `VK_PIPELINE_STAGE_VERTEX_SHADER_BIT`，但 uniform read 可能发生在 fragment 或 compute shader。上传路径通过 `cmd_update_sub_resource()` 先写入 `CopyDst`，随后转到 `ConstBuffer`。若目标 stage 不覆盖实际消费者，transfer write 不在该 shader read 的可见范围内，属于同步契约错误。DX12 的 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER` 不按 shader stage 拆分，不存在同类映射缺口。

本 SDD 来自 2026-07-11 全引擎 review 的 RHI synchronization finding。复核后将其中 UAV 部分降级为潜在能力缺口：当前粒子、TAA、体积光和 indirect self-test 没有同一资源连续 overlapping UAV RAW/WAW；现有路径均为 UAV→SRV、UAV→IndirectArgs、不同 ping-pong 资源或明确不重叠的只读区间。因此本阶段不新增未被生产代码使用的 UAV dependency API。

实施期的故障注入还复现出一个独立 P0：self-test 按预期返回失败后，Application 在首帧前 fail-fast；此时 Vulkan `currentFrame` 仍为 `UINT32_MAX`。三次失败注入与一次“self-test PASS 后仅强制早退”都先在 `VulkanSampler::~VulkanSampler()` 无条件索引 current-frame deletion queue 时触发 `0xC0000005`，排除了 self-test 像素/资源破坏；正常首帧把 `currentFrame` 置 0 后不崩。sampler 局部 guard 消除第一个崩点后，同一 RED 随即在 `VulkanTexture::~VulkanTexture()` 的相同 queue accessor 再次崩溃；只读审计确认 Buffer/View、Framebuffer、RenderPass、Texture/View、Sampler 与 StagingBuffer 都共享该 accessor。accessor 将 sentinel 映射到 queue 0 后，所有 deque 越界消失，但 teardown 在 VMA 销毁时报告 7 个 live allocation 并卡在 `VmaDeviceMemoryBlock::Destroy`：2 个 AO texture、4 个 deferred-light mesh buffer、1 个 volumetric buffer 仍被 `pendingTextureUploads` / `pendingBufferUploads` 持有。正常路径只在 `begin_frame()` 消费这些 pending uploads；早退路径从未进入 begin frame，shutdown 也未释放它们。根因因此是 pre-frame teardown 契约同时缺少“有效 deletion queue”与“在 drain/VMA 销毁前丢弃永远不会提交的 pending upload ownership”。用户已在本轮 review 起始授权明显 P0 直接修复，本 SDD 将两者作为 `VulkanContext` 内的一个原子 teardown 修复。

Task 4 质量审查又发现同一失败传播在 DX12 上没有对称闭环。DX12 初始化阶段同样会把首帧前 initial-data resource 放进 `m_pendingBufferUploads` / `m_pendingTextureUploads`；`DX12Context::shutdown()` 却先 flush 既有 deletion queue、销毁 descriptor heaps 与 D3D12MA allocator，直到 context 成员析构才释放 pending shared_ptr。临时把 compute expected red 从 17 改为 20 的故障注入在 120.695 秒内未正常返回：精确 mismatch 后命中 D3D12MA line 8027 `Some allocations were not freed before destruction of this memory block!` assertion，最后由 readiness watchdog 强制 exit 1。该 P1 不能用“进程最终非零”掩盖，必须在 DX12 首次 deletion-queue drain 前释放未提交 upload ownership。

同一审查还证明 GPU self-test 自身有两个 validation/可信度缺口：fresh render target 从 `Unknown` 直接 begin pass，而 Vulkan dynamic rendering 与 DX12 begin pass 都不隐式 transition attachment；现有 indirect self-test 有同类旧问题。另一个问题是两个 ConstBuffer transition 与 unrelated `computed_result -> UAVCompute` 被合进同一次 Vulkan legacy barrier，后者会把 COMPUTE stage 注入全批次，使 ConstBuffer policy 即使回归为缺 compute 也可能得到正确 tile。修订设计要求显式 `render_target -> RTV`，并把 unrelated UAV transition 拆成独立调用。

修正 GPU diagnostic 后，计划要求的“双 flag 同进程”DX12 GREEN 暴露出第三个独立缺陷：单独运行 constant-buffer self-test 连续两次均 PASS，但先运行 indirect、再运行 constant-buffer 时连续两次稳定在 compute tile 读到全零；把两份 self-test 源精确恢复到 `b0ab7f6` 后，同一双 flag 命令仍然稳定复现，排除了显式 RTV、barrier 分批与移除重复 idle 引入回归。frame 0 fence 的 value flow 已确认 indirect submit/wait 到 1、其 idle 到 2、constant submit/wait 到 3；临时加回 constant self-test 的额外 `wait_idle()` 仍失败，因此不是缺少 GPU completion wait。

第一轮只读假设把问题指向 READBACK 创建时使用 `{0,0}` read range。经用户批准后完成的最小 A/B 将 READBACK 改为创建时不 Map、读取时 full-range Map；Sandbox Debug 构建成功，但第一次 DX12 combined 仍得到完全相同的 indirect PASS / constant 全零 FAIL。实验随即按 stop rule 用 `apply_patch` 恢复，`DX12Buffer.h/.cpp` target diff 为空，未暂存或提交。Microsoft 文档同时明确所有 CPU-accessible heap 都支持 persistent mapping，且 Map 本身不承担 GPU 同步；现有 `{0,0}` 与实际 CPU read intent 不一致，可作为后续工具友好性审计，但已被 A/B 排除为本次 correctness 根因，本 SDD 不再修改 DX12Buffer。

恢复后从坏值反向追踪，静态证明真正根因是 DX12 shader-visible descriptor table cache 的 ABA 身份复用。cache key 仅包含 heap type、descriptor count 与 CPU descriptor handle 地址；CPU heap 却通过 LIFO free-list 立即复用地址，而 table cache 只在 `begin_frame()` 清空。indirect self-test 的 dispatch/draw UAV 单项 table 分别缓存 slot 1/3；函数退出按 slot 2、3、0、1 回收后，constant self-test 的 compute CBV 首先复用 slot 1。compute program 仍为每 binding 一个 count=1 table，故新 CBV key 命中旧 dispatch UAV 的 GPU table，跳过 descriptor copy；shader 在 CBV root table 实际读到旧 UAV descriptor并稳定得到零。该链精确解释 constant-only PASS、combined compute tile 全零、额外 idle/remap 均无效。它是生产级 P1：任一同帧 view 销毁/重建都可能发生相同 silent descriptor alias。

## Goals

- 让 Vulkan `ConstBuffer` barrier 覆盖当前公共 graphics/compute program 可发生的全部 uniform read stage。
- 保持 DX12 与 Vulkan 的 uniform-buffer 可见性语义等价。
- 用确定性 policy 单测先复现错误，再用双后端 GPU 精确读回验证 fragment/compute 消费链路。
- 不扩大普通 UAV 绑定的同步范围，不给 disjoint UAV write 增加隐式 barrier。
- 保证 Vulkan self-test 首帧前失败时所有既有 deferred-deletion caller 都获得有效 queue，且 pending upload 不再把 VMA 资源持有到 allocator 销毁；原始失败仍以非零退出传播。
- 保证 DX12 首帧前失败也在 descriptor heaps 与 D3D12MA allocator 有效时释放未提交 pending upload，并由正常 teardown 返回原始失败。
- 让 constant-buffer/indirect GPU self-test 显式声明 attachment 状态，并确保无关 UAV barrier 不能掩盖 ConstBuffer compute-stage 回归。
- 让 DX12 descriptor table cache 用“CPU handle 地址 + allocation serial”识别 descriptor 内容；同一活跃 allocation 保持 cache hit，slot 复用必须 cache miss 并重新复制。

## Non-goals

- 不新增 `cmd_uav_barrier`、`AshBarrier` dependency intent 或 RenderGraph UAV overlap/disjoint metadata。
- 不把任意 `UAV→UAV` 状态请求解释为内存依赖。
- 不修改 RenderGraph compiler/executor、异步 compute、queue ownership 或 synchronization2 路径。
- 不拆分 `ConstBufferGraphics` / `ConstBufferCompute` 公共状态，也不改 RHI ABI。
- 不为当前 Function program surface 尚未暴露的 tessellation、geometry、mesh 或 ray-tracing uniform binding 预留 stage；这些能力出现真实消费者时单独设计。
- 不逐个改造 Vulkan 资源析构器，不把 sentinel 伪装成一个已开始的 frame，不在 shutdown 录制/提交已失去消费者的 pending uploads，也不改变 queue flush 顺序。
- 不逐个改造 DX12 resource/view 析构器，不在 shutdown 录制 pending uploads，不把 descriptor cache 修复扩大为 view lifetime、D3D12MA 或 deferred-deletion 重构。
- 不清空整帧 descriptor cache，不在 view free 时扫描/反向索引 cache，不改变 descriptor heap partition、容量或 `begin_frame()` 时机。
- 不改变 `Buffer::get_mapped_data()`、fence、command allocator、copy queue 或 Vulkan descriptor 路径。
- 不修改或 bless RenderGate golden、PerfGate baseline。

未来出现至少两个真实 overlapping UAV RAW/WAW 调用点，或独立批准的 SDD 明确要求时，再按已确认的分层原则设计：低层 RHI 显式 opt-in dependency；RenderGraph 根据声明安全推导，并允许调用者明确标记 disjoint 后跳过。

## Current implementation

- Entry points:
  - `RenderDevice::collect_program_resource_barriers()` 为 graphics/compute program 的 uniform buffer 统一提交 `AshResourceState::ConstBuffer`。
  - `VulkanCommandBuffer::get_vk_stage_and_access_flags()` 把 `ConstBuffer` 映射为 vertex stage + uniform read access。
  - `DX12Helper::ash_to_d3d12_resource_state()` 把 `ConstBuffer` 映射为 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER`。
- Modules:
  - Graphics：资源状态到 native stage/access/state 的转换。
  - Function/Render：program binding、GPU self-test 生命周期与失败传播。
  - EntryPoint/Application：命令行 self-test 开关。
- Data flow:
  1. CPU/transfer 更新 GPU-only constant buffer；
  2. command buffer 转换到 `ConstBuffer`；
  3. graphics 或 compute program 绑定 CBV；
  4. shader 读取常量；
  5. self-test 把结果写入 RGBA8 target 并精确读回。
- Known constraints:
  - 仅靠 GPU 输出不能保证旧实现在所有驱动上稳定失败；错误同步可能碰巧得到正确值，因此需要无 GPU 的确定性 policy RED 测试。
  - self-test 在首帧前执行；其同步 immediate-submit 资源继续使用 `immediate_deletion = true` 做确定性回收，但引擎初始化阶段的 pending upload 仍可能持有其他非-immediate resource，backend shutdown 必须主动释放这些 ownership。
  - validation callback 当前只写日志；验证 harness 必须在进程启动前记录全部日志文件 byte offset，只扫描本次新增字节，断言 backend validation-enabled marker，并把新增 error/corruption 命中视为失败；不得扫描按分钟命名且可能 append 的“latest log”。
  - `Buffer::get_mapped_data()` 不提供同步；调用者必须先等待对应 GPU fence。READBACK full-range remap 已经 A/B 证伪并恢复，不再属于本次改动。
  - DX12 descriptor table cache 的内容身份不能只依赖可回收 CPU slot 地址；allocation serial 必须从 CPU heap allocation 传播到 binder 与 table key，数组中任一元素重新分配都必须 miss。
  - PerfGate baseline 为空，普通 `PASS + MISSING` 只能证明绝对门槛，不能证明相对性能无回归。
  - 当前 `VulkanCommandBuffer` 都来自 graphics-capable queue family，因此 vertex + fragment + compute stage union 合法；未来 dedicated compute-only command buffer 必须把该 policy 改成 queue-aware，不能在 compute-only queue 使用 graphics stage bit。

## Proposal

### Stage contract

`AshResourceState::ConstBuffer` 保持现有公共枚举与 ABI，语义明确为：可被 `GraphicsProgram` 或 `ComputeProgram` 的 shader stage 读取。

当前 Function program surface 明确为 vertex/fragment graphics program 与 compute program。Vulkan 映射只覆盖这三个真实 shader stage，不使用 aggregate 的 `ALL_GRAPHICS` / `ALL_COMMANDS`：

```cpp
VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
```

access 仍为 `VK_ACCESS_UNIFORM_READ_BIT`。这不把 vertex input、draw indirect、attachment、transfer 或当前没有 uniform-binding consumer 的 optional shader stage 纳入 scope。

DX12 的 ConstBuffer native state 映射不改；仅补齐独立发现的首帧前 teardown ownership，并作为跨后端 control 跑同一 GPU self-test。

### Deterministic policy seam

新增 Vulkan 内部 constexpr policy header，直接返回生产代码使用的 `VkPipelineStageFlags`。`VulkanCommandBuffer.cpp` 必须只通过该函数取得 ConstBuffer stage mask；doctest include 同一 header，直接验证 native flags。该 seam 不导出 DLL symbol，不进入公共 RHI API；本 SDD 明确批准它用于消除 GPU 未定义行为导致的非确定性测试盲区。

TDD 顺序必须保持：

1. 先把现有 vertex-only mask 原样提取到 policy header，并让 `VulkanCommandBuffer.cpp` 消费它；构建与既有测试保持 GREEN，证明只是 source-of-truth 提取。
2. 再写期望 vertex + fragment + compute 的 doctest；此时同一 production policy 仍为 vertex-only，测试必须 RED。
3. 最后只修改 policy 返回值为三个 stage 的 union，定向测试恢复 GREEN。

Tests 工程增加仓库内 Vulkan SDK include path，使 doctest 能检查真实 `VK_PIPELINE_STAGE_*` 常量。这样不存在 abstract domain 到 native flags 的第二套转换，也不能出现 policy 测试通过但生产仍使用另一份 mask。

单测必须先在旧语义下 RED，并断言：

- `ConstBuffer` 包含 graphics domain；
- `ConstBuffer` 包含 compute domain；
- mask 精确等于 vertex | fragment | compute，不包含 `ALL_GRAPHICS`、`ALL_COMMANDS` 或其他 stage。

### GPU self-test

新增独立 `--rhi-selftest-constant-buffer`，不复用或改名 `--rhi-selftest-indirect`。同一次首帧前 command recording 中完成两个子例：

1. GPU-only 256-byte constant buffer 经 update/CopyDst→ConstBuffer 后由 compute shader 读取，计算结果写入 storage buffer；后续 fragment shader 读取结果并输出到固定 tile。
2. 独立 GPU-only 256-byte constant buffer 经相同上传与 barrier 后由 fragment shader 直接读取并输出到另一固定 tile。

输出使用小型 RGBA8 render target 与 256-byte aligned row pitch。readback 对每个 tile 的内部区域逐像素检查预期 RGB（容差 ≤ 1）和 alpha 255；不得只检查单像素或使用 SSIM。任一资源创建、view/bind、update、transition、program apply、command-buffer 状态或 mapped readback 失败都使 self-test 失败，并传播为应用非零退出。

两个 ConstBuffer 可在同一 transition 调用中处理，因为它们使用同一 stage policy；`computed_result -> UAVCompute` 必须是独立 transition，禁止其 COMPUTE stage union 掩盖 ConstBuffer policy 回归。fresh render target 必须在 begin pass 前显式 transition 到 `RTV`；既有 indirect self-test 的同类直接 begin-pass 路径同步修正。`submit_immediately()` 在两后端都等待提交 fence，因此 self-test 不再追加 device-wide `wait_idle()`。

CI 的 WARP/lavapipe readiness 命令同时传入 indirect 与 constant-buffer 两个 self-test flag，使 Debug 本地 validation 与 Release 软件后端都运行同一逻辑。

### Pre-frame teardown contract

- Vulkan：`currentFrame == UINT32_MAX` 映射到 deletion queue 0；shutdown 在第一次 drain 前锁内 swap pending buffer/texture upload vectors、锁外析构，使资源析构安全入 queue 0，再由既有 flush 回收。
- DX12：`m_currentFrame` 首帧前已为 0，不需要 sentinel 映射；shutdown 在第一次 deletion-queue flush 前同样锁内 swap 两个 pending vector、锁外析构。资源/view 析构在 context、descriptor heap 与 D3D12MA allocator 仍有效时把工作排入 queue 0，随后的既有首次 flush 必须把它们全部回收。
- 两后端都只丢弃从未录制/提交的 CPU upload payload 与 shared ownership，不在 teardown 创建 GPU 工作，不改变正常 begin-frame upload 流程或 queue flush 顺序。

### DX12 descriptor table identity contract

- `DX12CPUDescriptorHeap::allocate()` 在既有 allocation mutex 内为每次 allocation 分配非零、单调递增的 `uint64_t allocationSerial`；同一 slot 被 free 后再次 allocate 必须获得新 serial。serial 只描述 CPU descriptor allocation identity，不参与 GPU heap offset 或资源生命周期；若计数达到 `UINT64_MAX`，allocation 必须 fail closed，不能 wrap 后复用旧 identity。
- `DX12DescriptorHandle` 携带 CPU handle、GPU handle、heap index 与 allocation serial。render-program binder 必须保存完整 handle；不得在进入 cache 前丢弃 serial 只保留裸 `D3D12_CPU_DESCRIPTOR_HANDLE`。
- 当前 CBV/SRV/UAV 与 Sampler 都在 allocation 后写入 descriptor 一次，生命周期内不原地覆盖，析构时才 free；因此 allocation serial 等价于内容版本。未来若引入同一活跃 handle 的原地 descriptor rewrite，必须同时分配新 identity 或显式推进 serial，并补相应 cache test，不能继续复用旧 serial。
- table cache key 对每个 descriptor 使用 `{cpuHandle.ptr, allocationSerial}`，同时保留现有 heap type、descriptor count 与数组顺序。生产与确定性 doctest 共用同一个内部 key factory；同 ptr/同 serial 必须命中，同 ptr/不同 serial、数组任一 serial 变化、heap/count/order 变化必须 miss。
- cache hit 路径、活跃 descriptor 的去重率与 shader-visible partition 不变。slot 复用只会使新内容第一次绑定 miss 并执行既有 `CopyDescriptorsSimple`；不在 free 路径扫描或清空 cache，不增加新的销毁锁或反向索引。
- context/heap 重建会先清空 cache，因此 serial 可从 1 重新开始；单次 heap lifetime 内不允许 serial wrap。当前 manager cache 与 GPU heap 的既有线程约束不在本修复中扩展。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Graphics/Vulkan | 提取内部 shader-scope policy；修正 `ConstBuffer` stage | `Vulkan/VulkanBarrierPolicy.h`、`Vulkan/VulkanCommandBuffer.cpp` |
| Graphics/Vulkan teardown | accessor 为首帧前 sentinel 提供 queue 0 fallback；shutdown 在 drain 前释放 pending upload ownership | `Vulkan/VulkanContext.h/.cpp` |
| Graphics/DX12 teardown | shutdown 在首次 drain 前释放未提交 pending upload ownership | `DirectX12/DX12Context.cpp` |
| Graphics/DX12 descriptor cache | CPU allocation serial 贯穿 handle/binder/cache key，阻止 slot ABA 命中旧 GPU table | `DirectX12/DX12Helper.hpp`、`DX12DescriptorHeap.h/.cpp`、`DX12RenderProgramBinder.h/.cpp`、`DX12RenderProgram.cpp` |
| Function/Render | 新增 constant-buffer GPU 精确读回 self-test；修正 constant/indirect self-test 的显式 RTV 状态 | `RHIConstantBufferSelfTest.h/.cpp`、`RHIIndirectSelfTest.cpp` |
| Shaders | 新增 self-test compute/vertex/fragment entry points | `Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl` |
| Application/CLI | 新增 opt-in flag 并传播失败 | `EntryPoint.h`、`Function/Application.h/.cpp` |
| Tests | 新增确定性 Vulkan barrier policy、DX12 descriptor cache identity doctest、CLI parsing coverage，并加入 Vulkan SDK header include | `project/src/tests/Graphics/vulkan_barrier_policy_tests.cpp`、`project/src/tests/Graphics/dx12_descriptor_table_cache_tests.cpp`、`project/src/tests/Function/application_automation_tests.cpp`、`project/src/tests/premake5.lua` |
| CI | WARP/lavapipe smoke 启用新 self-test | `.github/workflows/ci.yml` |
| Docs | 回写 ConstBuffer stage 契约、验证入口、命令行契约与仓库验证入口 | `docs/specs/modules/graphics.md`、`docs/specs/modules/application.md`、`README.md` |

### API / contract changes

- 新增内部命令行验证开关 `--rhi-selftest-constant-buffer`；默认关闭，不改变普通启动。
- `AshResourceState::ConstBuffer` 的公共类型和数值不变，只修正 Vulkan 对既有语义的实现。
- 不新增 Graphics virtual method，不改变 `AshBarrier` 构造函数或 tracker 数据结构。
- Vulkan deletion queue 的公共接口不变；`currentFrame == UINT32_MAX` 从越界访问改为返回 queue 0，有效 frame 仍返回原 frame queue。shutdown 丢弃尚未录制的 pending upload CPU payload 与 resource shared_ptr，不向 GPU 提交新工作。
- DX12 公共接口不变；shutdown 在既有首次 deletion-queue flush 前丢弃尚未录制的 pending upload CPU payload 与 resource shared_ptr，不向 GPU 提交新工作。
- DX12 公共 RHI 接口不变；内部 `DX12DescriptorHandle` 增加 allocation serial，binder/cache API 改为传递完整 internal handle。

### Backend impact

- Vulkan：stage scope 从 vertex-only 修正为 vertex + fragment + compute；layout/access 不变。
- DX12：ConstBuffer native state 映射不变；补齐首帧前 pending-upload teardown，并修正 descriptor table cache 的 CPU slot ABA identity，运行同一 GPU self-test防止验证逻辑写成 Vulkan 特化。
- 两后端 validation/debug-layer error 均视为失败，不允许关闭 validation 规避。

### Performance

- 普通帧不新增 command、barrier 或 allocation；只扩大已有 ConstBuffer barrier 的 Vulkan destination/source stage scope。诊断 self-test 增加必要 RTV transition 并拆分既有 barrier batch。
- scope 是显式 vertex + fragment + compute union，不使用 `ALL_GRAPHICS` / `ALL_COMMANDS`，也不触碰 UAV path。
- deletion queue accessor 增加一次 sentinel 比较，只在资源释放路径执行；pending upload release 只在 shutdown 执行；有效 frame 的 upload/queue 选择与 flush 时机不变。
- DX12 pending upload release 同样只在 shutdown 执行；self-test 增加必要的 RTV barrier、拆分现有 barrier batch，并移除重复 device-wide idle，不影响普通帧热路径。
- DX12 CPU descriptor allocation 在既有 mutex 临界区多写一个 64-bit serial；table key 每项多 hash/compare 一个 64-bit 值。活跃 allocation 的 cache hit 与 descriptor copy 数量不变，只有复用 slot 的新内容按正确性要求发生首次 miss；free 路径无 cache 扫描/清空。
- 由于 PerfGate baseline 为空，实施前后在同一 worktree、相同机器、GPU 独占条件下各跑一次 Standard profile。按现有 profile warn 阈值比较 CPU avg/P95/P99、private bytes、engine heap 和 draw calls；超过阈值则阻断并复核。普通 PerfGate `FAIL` 同样阻断，`WARN` 必须解释。

## Verification plan

| 验证 | 覆盖 | 命令 / 证据 |
| --- | --- | --- |
| Baseline | 最新 main 单测基线 | `RunTests.bat Debug`；设计阶段已得到 50/50 cases、545/545 assertions |
| Deterministic RED/GREEN | ConstBuffer shader-domain policy | `RunTests.bat Debug --test-case="*Vulkan barrier policy*"` |
| Full unit | 全量 doctest 与 legacy bridge | `RunTests.bat Debug` |
| Build | Engine + Editor/Sandbox 接线 | fresh `generate_vs2022.bat`；`build_editor.bat Debug`；`build_sandbox.bat Debug`；`build_sandbox.bat Release` |
| Application lifecycle | 新 CLI 与 fail-fast 传播不破坏应用生命周期 | `run.bat all Debug --smoke-test-seconds=120`；Editor 打开默认场景，操作视口/实体/面板后正常退出 |
| GPU self-test | fragment/compute CB visibility | `run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer`；DX12 同命令 |
| Combined diagnostics | 同一 Application 生命周期内 descriptor slot 回收/复用 | Vulkan/DX12 均同时传 `--rhi-selftest-indirect --rhi-selftest-constant-buffer`；两项都须 PASS。DX12 RED 已稳定复现 slot 1 旧 UAV table 被新 CBV key 误命中；修复后连续两次必须 GREEN |
| DX12 cache identity | descriptor allocation ABA 的确定性 policy | 定向 doctest 覆盖同 ptr/同 serial hit；同 ptr/不同 serial、数组元素 serial、heap/count/order 变化 miss；临时 bypass-cache A/B 只作根因确认并在正式实现前恢复 |
| Failure-path RED/GREEN | 两后端首帧前失败均由正常 teardown 在有界时间内返回非零，不能由 watchdog 强杀掩盖 hang/leak | 临时把 compute expected red 从 17 改为 20，分别跑 Vulkan 与 DX12。两者都须保留精确 mismatch/FAIL 与 wrapper exit 1，并在 120 秒 watchdog 前由 Application 正常完成 teardown。Vulkan 日志必须包含 `VMA leak tracking: no live VMA allocations detected before allocator shutdown.`；DX12 日志必须包含 `DX12Context: Shutdown complete.`。两者均不得包含 `Fatal Error: readiness process deadline expired`、live-allocation/leak、`Assertion failed`、D3D12MA `Some allocations were not freed` 或 `0xC0000005`，不得 hang、crash 或 forced exit。验收后再精确恢复 expected red |
| Validation | Vulkan sync/standard validation；DX12 debug/GPU validation | 隔离 worktree内临时启用对应 Engine.ini 开关；运行前快照每个 logfile 的 byte length，只扫描本次 appended/new bytes；要求 Vulkan bundled-layer marker 或 DX12 debug+GPU-validation marker，error/corruption pattern 命中即非零；finally 精确恢复配置 |
| Software backend | WARP/lavapipe 兼容 | CI Release smoke 同时运行 indirect 与 constant-buffer self-test |
| Architecture | 依赖方向与新增 include | `RunArchGate.bat` |
| Render regression | 双后端 golden 与 cross-backend | `RunRenderGate.bat`；禁止 bless |
| Performance | 绝对门槛 + 同机配对趋势 | 实施前后 `RunPerfGate.bat -Profile Standard`；禁止 bless baseline |
| Plan/tooling | dirty path 验证矩阵 | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan` |
| Integrity | 提交边界与 whitespace | `git diff --check`；仅暂存 Module changes 表列出的本任务文件 |

GPU/RenderGate/PerfGate 与其他活动会话串行执行，避免共享 GPU 采样与运行时配置互扰。

实施开始前重新读取所有 active worktree 的 branch/status/diff，确认 `EntryPoint.h`、`Application.*`、`project/src/tests/premake5.lua`、`.github/workflows/ci.yml` 与 `docs/specs/modules/graphics.md` 的所有权。若其他会话已修改任一热点文件，本任务等待其形成提交并从新 main 重放，或由双方明确拆分 hunk；禁止同时编辑后再整文件暂存。

## Task breakdown

1. 提交本 SDD 并取得用户最终批准；批准前不改生产代码。
2. 记录实施前 Standard PerfGate 配对基线，不更新仓库 baseline。
3. 把 vertex-only native mask 行为保持地提取到 policy header并接入生产代码，先验证既有测试保持 GREEN；再新增 policy doctest，运行定向测试确认 RED。
4. 只修改 policy source-of-truth 为 vertex + fragment + compute，运行定向测试确认 GREEN。
5. 新增双后端 constant-buffer GPU self-test 与 CLI/CI 接线；显式处理 self-test RTV 状态并隔离 CB/UAV barrier batch；用双后端故障注入复现首帧前 pending ownership 导致的 Vulkan/VMA 与 DX12/D3D12MA teardown failure；两后端都在首次 drain 前释放未提交 upload ownership，Vulkan 另由唯一 accessor 把 `UINT32_MAX` 映射到 queue 0。
6. 保留已记录的 DX12 combined RED；先临时 bypass table cache lookup 做一次根因 A/B，预期两项 PASS 后立即恢复。再以确定性 doctest RED 驱动 allocation serial 贯穿 CPU handle、binder 与 cache key，禁止清整帧 cache或在 free 路径扫描。
7. 确认两后端故障注入均由正常 teardown 非零退出且不崩溃/挂起，再恢复 expected value 并跑双后端 combined GREEN。
8. 跑 fresh build、全量单测、ArchGate、双后端 validation/readiness、RenderGate、Standard PerfGate 与 AIDevDoctor。
9. 独立代码审查；关闭 finding 后回写 `docs/specs/modules/graphics.md`，把本 SDD 标记 Done。
10. 精确暂存、提交并按用户选择集成；不触碰其他 worktree 或共享 workspace dirty 文件。

## Risks

| Risk | Mitigation |
| --- | --- |
| 扩大 stage scope 影响性能 | 仅显式 vertex + fragment + compute；不使用 all-graphics/all-commands；同机前后 PerfGate 配对比较 |
| GPU self-test 在错误实现上碰巧通过 | policy doctest 提供确定性 RED；GPU test 只负责真实双后端链路 |
| unrelated UAV barrier 的 COMPUTE stage 掩盖 CB policy 回归 | `computed_result -> UAVCompute` 单独 transition；CB batch 不混入其他 stage domain |
| fresh self-test RT 未显式进入 attachment state | constant/indirect self-test 均在 begin pass 前检查 `Unknown -> RTV` transition；validation 双后端扫描 |
| self-test 本身只覆盖一个 shader stage | 独立 compute 与 fragment 子例，逐 tile 精确读回 |
| validation 只写日志但进程仍成功，或 latest log 混入旧消息 | harness 以运行前 byte offsets 切出 fresh delta，断言 validation-enabled marker；本次 error/corruption 命中即判失败，旧字节不参与判断 |
| 自测录制失败后 command buffer 不可安全复用 | 两个请求的 self-test 都执行；任一失败在 `_on_startup` 与首帧前 fail-fast 返回非零，不进入应用循环 |
| 首帧前 fail-fast 时 backend pending upload 持有 allocator 资源，Vulkan 还会索引无效 frame | 两后端 shutdown 都在首次 drain 前 swap/drop pending ownership；Vulkan accessor 对 `UINT32_MAX` 返回 queue 0；析构器安全入 queue 0 后由既有 flush 回收；双后端故障注入验证正常非零退出且无崩溃/挂起/live allocation |
| DX12 cache 把可回收 CPU slot 地址误当 descriptor 内容 identity | 每次 CPU descriptor allocation 分配 serial，cache key 使用 ptr+serial；combined self-test 覆盖真实 slot 1 UAV→CBV ABA，doctest 覆盖单项/数组 identity |
| 修复退化为 free-time 全 cache clear/scan，造成性能与锁风险 | serial 在既有 allocate mutex 内生成；free 路径不变；活跃 handle hit 率不变，仅复用新内容正确 miss；PerfGate 与 descriptor heap overflow 日志共同门禁 |
| serial 在 binder/cache 传播中被截断或数组遗漏 | pending bind 保存完整 internal handle；生产 key factory 同时处理 inline/overflow；doctest 覆盖同地址不同 serial 和数组任一元素变化 |
| 测试 seam 变成公共抽象 | header 保持 Vulkan internal、无导出 symbol；仅生产映射与 doctest 使用 |
| 未来 dedicated compute queue 不允许 graphics stage bit | 当前 command buffer 固定来自 graphics-capable family；async compute 属于 Non-goal，未来引入时把 policy 改为 queue-aware 并补 queue-capability 测试 |
| 与并行粒子/terrain 工作冲突 | 独立 worktree/branch；实施前复核热点文件所有权；重叠时等待对方提交并从新 main 重放；GPU 门禁串行 |
| 把潜在 UAV 能力再次误报成当前故障 | UAV 明确列为 Non-goal；实际调用点审计结论写入 Context；无真实消费者前不实现 |

## Implementation outcome

- Vulkan `AshResourceState::ConstBuffer` 已精确映射为 vertex + fragment + compute shader stage，access 保持 `VK_ACCESS_UNIFORM_READ_BIT`；DX12 native state 不变。实现未新增 UAV→UAV barrier、隐式 UAV dependency、RenderGraph barrier 或公共 RHI barrier API。
- 新增 opt-in `--rhi-selftest-constant-buffer`，分别覆盖 compute 与 fragment uniform read；与 indirect self-test 独立执行，任一失败都在 `_on_startup()` 与首帧前传播为非零退出。两个 self-test 均显式进入 RTV，ConstBuffer transition 与 UAV transition 分批，录制错误保留高层阶段与底层 command-buffer 原因。
- Vulkan deletion queue accessor 对首帧前 sentinel 返回 queue 0；Vulkan/DX12 shutdown 均在首次 deletion-queue drain 前锁内 swap、锁外释放从未提交的 pending uploads，使资源在 allocator、descriptor heap 与 device 有效时进入既有回收路径。
- DX12 CPU descriptor allocation 增加非零、单调且禁止回绕的 serial；shader-visible table cache key 使用 heap/count/order 与每项 `{address, serial}`，scalar/array、buffer/texture/sampler 均传播完整 handle。同一活跃 allocation 仍命中；slot ABA 复用正确 miss；free 路径没有扫描、反向索引或清空 cache。
- 故障注入把 compute expected red 从 17 临时改为 20：Vulkan 与 DX12 都精确报告 actual `(17,101,203,255)` / expected `(20,101,203,255)`，分别在 3.076s / 1.838s 内由正常 teardown 返回 1；Vulkan 报告零 live VMA allocation，DX12 报告 shutdown complete，均无 watchdog、assert、allocator outstanding allocation 或 access violation。坏值随后精确恢复并重建。
- 最终独立审查覆盖 `origin/main...HEAD` 全部 28 个文件：Critical 0、Important 0、Minor 0，SDD compliance 通过，Ready=Yes。

### Verification evidence

- Fresh generate；Editor Debug、Sandbox Debug/Release 构建通过。
- doctest：66/66 cases、858/858 assertions；ArchGate PASS（35 条既有 legacy warning，未增长）。
- `run.bat all Debug --smoke-test-seconds=120` 四组合 readiness PASS。
- Vulkan bundled validation 与 DX12 debug layer + GPU validation 下 combined indirect/constant-buffer self-test 均 PASS，fresh process output 的 error/corruption reject pattern 为 0；DX12 仅有已知 MessageID 820 optimized-clear 性能 warning，与本 SDD 无关。
- RenderGate PASS（report `20260713-173511-643-51952-33b92d67`）：sandbox Vulkan/DX12 SSIM 0.996278/0.996177、cross 0.999747；particles Vulkan/DX12/cross 均为 1.0；未 bless。
- 实施基线与变基前 HEAD 的同刻 B-A-B Standard PerfGate 均 PASS、无 WARN/FAIL，heap/draw 稳定。UE 编译结束并取得 CPU/GPU 全独占后，最终 post-rebase Standard PerfGate PASS（report `20260713-183246`），四组合 Failures/Warnings 均空；Sandbox Vulkan/DX12 avg/P95 为 7.3364/9.3697ms、7.9459/10.6882ms，Editor Vulkan/DX12 为 9.5280/11.0667ms、10.0091/11.8899ms。高负载期间的 `20260713-173801` report 已作废且未纳入证据。
- 每次临时 validation 配置均恢复 `product/config/Engine.ini` 至 SHA-256 `FF5E59BD907F3A5C0CCCFB8C1743AC472B7F68ADFD1AC8FE78AD9FE9C35AE645`、447 bytes；运行时 ImGui layout diff 已恢复，工作树 clean。
- 真实 Editor 鼠标/键盘/面板交互按仓库职责边界留作人工验收；Agent 未自动驱动 UI，也未把 readiness smoke 冒充交互验收。

## Rollback

- DX12 teardown 修复与 diagnostic transition 修复必须保持独立提交。若最终 expected-20 DX12 注入仍出现 allocator outstanding-allocation、assert、watchdog forced exit、hang 或 crash，阻断合并并 revert DX12 teardown 提交；同时撤下 constant-buffer CI flag 与“DX12 正常失败退场”spec/README 声明，不能把 forced exit 接受为基线。
- 若 explicit RTV / CB-UAV 分批修订引入任一后端 validation error、正向 self-test failure 或 RenderGate regression，阻断合并并独立 revert diagnostic correction；在重新设计前禁用新 constant-buffer CLI/CI 暴露，并同步撤回 application/graphics spec 与 README 中不再成立的诊断保证。不得 bless golden 或 baseline 来替代修复。
- DX12 descriptor cache identity 修复保持独立提交。若 bypass-cache A/B 不转 GREEN、allocation-serial 实现不能让 combined 连续两次 GREEN，或 PerfGate/descriptor partition 出现回归，立即恢复/回退该独立提交并阻断 constant-buffer CLI/CI 合并；不得用 self-test 间清 cache、额外 sleep/device idle、关闭 validation 或 bless 掩盖 ABA。
- Vulkan stage policy 与已验证的 Vulkan pre-frame P0 提交不依赖上述两个新增修订；rollback 不应整分支回退或破坏已通过的确定性 policy 测试。所有 rollback 后必须重跑最窄受影响验证、`RunTests.bat Debug`、`RunArchGate.bat` 与 `git diff --check`。

## Open questions

- None for this SDD.
