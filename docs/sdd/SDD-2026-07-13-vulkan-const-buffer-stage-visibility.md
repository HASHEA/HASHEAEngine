# SDD-2026-07-13-vulkan-const-buffer-stage-visibility: Vulkan 常量缓冲可见阶段修正

## Status

Approved

## Context

`AshResourceState::ConstBuffer` 是 Graphics 公共资源状态。Function 层对 graphics program 与 compute program 的 uniform buffer 都使用该状态；Material V2 已经在 graphics program 中绑定真实 uniform buffer，`ComputeProgram::set_uniform_buffer()` 也是公开可用接口。

Vulkan 后端当前把 `ConstBuffer` 的 pipeline stage 固定为 `VK_PIPELINE_STAGE_VERTEX_SHADER_BIT`，但 uniform read 可能发生在 fragment 或 compute shader。上传路径通过 `cmd_update_sub_resource()` 先写入 `CopyDst`，随后转到 `ConstBuffer`。若目标 stage 不覆盖实际消费者，transfer write 不在该 shader read 的可见范围内，属于同步契约错误。DX12 的 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER` 不按 shader stage 拆分，不存在同类映射缺口。

本 SDD 来自 2026-07-11 全引擎 review 的 RHI synchronization finding。复核后将其中 UAV 部分降级为潜在能力缺口：当前粒子、TAA、体积光和 indirect self-test 没有同一资源连续 overlapping UAV RAW/WAW；现有路径均为 UAV→SRV、UAV→IndirectArgs、不同 ping-pong 资源或明确不重叠的只读区间。因此本阶段不新增未被生产代码使用的 UAV dependency API。

## Goals

- 让 Vulkan `ConstBuffer` barrier 覆盖当前公共 graphics/compute program 可发生的全部 uniform read stage。
- 保持 DX12 与 Vulkan 的 uniform-buffer 可见性语义等价。
- 用确定性 policy 单测先复现错误，再用双后端 GPU 精确读回验证 fragment/compute 消费链路。
- 不扩大普通 UAV 绑定的同步范围，不给 disjoint UAV write 增加隐式 barrier。

## Non-goals

- 不新增 `cmd_uav_barrier`、`AshBarrier` dependency intent 或 RenderGraph UAV overlap/disjoint metadata。
- 不把任意 `UAV→UAV` 状态请求解释为内存依赖。
- 不修改 RenderGraph compiler/executor、异步 compute、queue ownership 或 synchronization2 路径。
- 不拆分 `ConstBufferGraphics` / `ConstBufferCompute` 公共状态，也不改 RHI ABI。
- 不为当前 Function program surface 尚未暴露的 tessellation、geometry、mesh 或 ray-tracing uniform binding 预留 stage；这些能力出现真实消费者时单独设计。
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
  - self-test 在首帧前执行，创建的 RHI resource/view 必须设置 `immediate_deletion = true`。
  - validation callback 当前只写日志；交付脚本必须扫描 Vulkan/DX12 validation error，并把命中视为失败。
  - PerfGate baseline 为空，普通 `PASS + MISSING` 只能证明绝对门槛，不能证明相对性能无回归。

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

DX12 不改实现，只作为跨后端 control 跑同一 GPU self-test。

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

CI 的 WARP/lavapipe readiness 命令同时传入 indirect 与 constant-buffer 两个 self-test flag，使 Debug 本地 validation 与 Release 软件后端都运行同一逻辑。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Graphics/Vulkan | 提取内部 shader-scope policy；修正 `ConstBuffer` stage | `Vulkan/VulkanBarrierPolicy.h`、`Vulkan/VulkanCommandBuffer.cpp` |
| Function/Render | 新增 constant-buffer GPU 精确读回 self-test | `RHIConstantBufferSelfTest.h/.cpp` |
| Shaders | 新增 self-test compute/vertex/fragment entry points | `Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl` |
| Application/CLI | 新增 opt-in flag 并传播失败 | `EntryPoint.h`、`Function/Application.h/.cpp` |
| Tests | 新增确定性 Vulkan barrier policy doctest，并加入 Vulkan SDK header include | `project/src/tests/Graphics/vulkan_barrier_policy_tests.cpp`、`project/src/tests/premake5.lua` |
| CI | WARP/lavapipe smoke 启用新 self-test | `.github/workflows/ci.yml` |
| Docs | 回写 ConstBuffer stage 契约与验证入口 | `docs/specs/modules/graphics.md` |

### API / contract changes

- 新增内部命令行验证开关 `--rhi-selftest-constant-buffer`；默认关闭，不改变普通启动。
- `AshResourceState::ConstBuffer` 的公共类型和数值不变，只修正 Vulkan 对既有语义的实现。
- 不新增 Graphics virtual method，不改变 `AshBarrier` 构造函数或 tracker 数据结构。

### Backend impact

- Vulkan：stage scope 从 vertex-only 修正为 vertex + fragment + compute；layout/access 不变。
- DX12：生产实现不变；运行同一 GPU self-test，防止验证逻辑写成 Vulkan 特化。
- 两后端 validation/debug-layer error 均视为失败，不允许关闭 validation 规避。

### Performance

- 不新增 command、barrier 数量或普通帧 allocation；只扩大已有 ConstBuffer barrier 的 Vulkan destination/source stage scope。
- scope 是显式 vertex + fragment + compute union，不使用 `ALL_GRAPHICS` / `ALL_COMMANDS`，也不触碰 UAV path。
- 由于 PerfGate baseline 为空，实施前后在同一 worktree、相同机器、GPU 独占条件下各跑一次 Standard profile。按现有 profile warn 阈值比较 CPU avg/P95/P99、private bytes、engine heap 和 draw calls；超过阈值则阻断并复核。普通 PerfGate `FAIL` 同样阻断，`WARN` 必须解释。

## Verification plan

| 验证 | 覆盖 | 命令 / 证据 |
| --- | --- | --- |
| Baseline | 最新 main 单测基线 | `RunTests.bat Debug`；设计阶段已得到 50/50 cases、545/545 assertions |
| Deterministic RED/GREEN | ConstBuffer shader-domain policy | `RunTests.bat Debug --test-case="*Vulkan barrier policy*"` |
| Full unit | 全量 doctest 与 legacy bridge | `RunTests.bat Debug` |
| Build | Engine + Editor/Sandbox 接线 | fresh `generate_vs2022.bat`；`build_editor.bat Debug`；`build_sandbox.bat Debug`；`build_sandbox.bat Release` |
| GPU self-test | fragment/compute CB visibility | `run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer`；DX12 同命令 |
| Validation | Vulkan sync/standard validation；DX12 debug/GPU validation | 隔离 worktree 内临时启用对应 Engine.ini 开关，双后端运行 self-test + readiness；扫描日志；finally 精确恢复配置 |
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
5. 新增双后端 constant-buffer GPU self-test 与 CLI/CI 接线；临时翻转一个预期常量或 tile 期望值做确定性故障注入，确认 self-test 非零退出后恢复，再跑双后端 GREEN。
6. 跑 fresh build、全量单测、ArchGate、双后端 validation/readiness、RenderGate、Standard PerfGate 与 AIDevDoctor。
7. 独立代码审查；关闭 finding 后回写 `docs/specs/modules/graphics.md`，把本 SDD 标记 Done。
8. 精确暂存、提交并按用户选择集成；不触碰其他 worktree 或共享 workspace dirty 文件。

## Risks

| Risk | Mitigation |
| --- | --- |
| 扩大 stage scope 影响性能 | 仅显式 vertex + fragment + compute；不使用 all-graphics/all-commands；同机前后 PerfGate 配对比较 |
| GPU self-test 在错误实现上碰巧通过 | policy doctest 提供确定性 RED；GPU test 只负责真实双后端链路 |
| self-test 本身只覆盖一个 shader stage | 独立 compute 与 fragment 子例，逐 tile 精确读回 |
| validation 只写日志但进程仍成功 | wrapper 扫描 error/corruption；命中即判失败 |
| 测试 seam 变成公共抽象 | header 保持 Vulkan internal、无导出 symbol；仅生产映射与 doctest 使用 |
| 与并行粒子/terrain 工作冲突 | 独立 worktree/branch；实施前复核热点文件所有权；重叠时等待对方提交并从新 main 重放；GPU 门禁串行 |
| 把潜在 UAV 能力再次误报成当前故障 | UAV 明确列为 Non-goal；实际调用点审计结论写入 Context；无真实消费者前不实现 |

## Open questions

- 无实现阻塞项。用户已批准本 S2 设计，可按 Task breakdown 进入实现。
