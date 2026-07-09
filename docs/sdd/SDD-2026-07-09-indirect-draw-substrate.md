# SDD-2026-07-09-indirect-draw-substrate: GPU-driven 基建——RHI indirect draw/dispatch（S2）

## Status
Done（2026-07-09）。验证结果：
- 双后端构建：Debug（editor+sandbox）/ Release（sandbox）均过
- 自测链路：Vulkan / DX12 × Debug / Release 全部 PASS（rgb=(0,200,0)），退出干净、日志无 validation 报错
- `RunTests.bat Debug`：16/16 cases、116/116 assertions
- `RunArchGate.bat`：PASS（35 legacy warns 均为既有名单）
- `RunRenderGate.bat`：三项 PASS（vulkan ssim=0.999919、dx12 ssim=0.999916、cross ssim=0.999442）
- 软渲染兼容：CI 两条冒烟命令已加 `--rhi-selftest-indirect`（WARP + lavapipe），push 后观察
- 实现期发现并规避：Vulkan 首帧前延迟删除队列越界（`currentFrame==UINT32_MAX`），预循环资源须置 `immediate_deletion`，已作为约束回写 graphics spec
- 结论已回写 `docs/specs/modules/graphics.md`（公共接口 + 跨后端契约 + 首帧前销毁约束）

## Context
三大系统路线（粒子→植被→地形）的阶段 0。GPU 粒子（GPU 侧决定实例数）与植被
（GPU 剔除后间接绘制）共同依赖 indirect draw，当前 RHI 无此 API。

按代码核实的现状：底座已齐，只缺入口——
- buffer usage：`ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT` 已定义且双后端已映射
  （`VulkanBuffer.cpp:50`；DX12 buffer 无 usage 特化，天然支持）
- barrier：`AshResourceState::IndirectArgs` 已存在且双后端映射完整
  （`DX12Helper.hpp:204` → `D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT`；
  `VulkanCommandBuffer.cpp:136` → `DRAW_INDIRECT` stage + `INDIRECT_COMMAND_READ` access）
- 实例化：`cmd_draw` / `cmd_draw_indexed` 已带 instanceCount / firstInstance
- 缺失：`CommandBuffer` 无 draw/dispatch indirect 虚接口；DX12 无 command signature 基建

## Goals
- `CommandBuffer` 新增三个跨后端等价接口：
  - `cmd_draw_indirect(buffer, offset, drawCount, stride)`
  - `cmd_draw_indexed_indirect(buffer, offset, drawCount, stride)`
  - `cmd_dispatch_indirect(buffer, offset)`
- RHI 层定义参数结构体（`AshDrawIndirectArgs` / `AshDrawIndexedIndirectArgs` /
  `AshDispatchIndirectArgs`），内存布局与 VK / D3D12 原生结构逐字段一致（两 API 布局本就相同）
- 定死跨后端 shader 契约（见 API/contract）
- 自验证：一条 CLI 开关驱动的最小链路自测（compute 写 args → barrier → indirect draw）

## Non-goals
- **count buffer 变体**（`vkCmdDrawIndexedIndirectCount` / DX12 count buffer）：粒子用
  "单 draw + GPU 写 instanceCount" 即可；多 draw 场景用 CPU 已知 drawCount + 空槽
  zero-padding（`instanceCount=0` 的 draw 开销可忽略）。植被 SDD 出现真实需求再加，
  且 Vulkan 侧 `drawIndirectCount` 是可选设备能力，届时按能力分级原则处理
- RenderGraph 的 buffer 资源声明/生命周期扩展——粒子 SDD 的事
- GPU 剔除、多 queue、mesh shader 路径

## Current implementation
- Entry points：`Graphics/CommandBuffer.h`（纯虚接口）、
  `Vulkan/VulkanCommandBuffer.cpp`、`DirectX12/DX12CommandBuffer.cpp`
- 已有先例：`cmd_dispatch` 双后端实现是最近似模板
- 设备能力：plain indirect draw 是 VK/DX12 核心能力，无 feature 查询需求
  （lavapipe / WARP 均支持，CI 软渲染冒烟可覆盖）

## Proposal

### Module changes
| Module | Change | Files |
| --- | --- | --- |
| Graphics | 三个纯虚接口 + 参数结构体定义 | `CommandBuffer.h`、`RHICommon.h` |
| Graphics/Vulkan | `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect` / `vkCmdDispatchIndirect` 直通 | `VulkanCommandBuffer.h/cpp` |
| Graphics/DX12 | 三个固定 command signature（draw / drawIndexed / dispatch，设备级缓存一次创建）+ `ExecuteIndirect` | `DX12CommandBuffer.h/cpp`、`DX12Context`（signature 缓存落点） |
| Function/Render | CLI 开关 `--rhi-selftest-indirect`：compute 写 args buffer → 转 IndirectArgs → indirect draw 小色块 + 日志 PASS 标记 | 自测挂在既有 debug 路径，默认关闭 |

### API / contract changes
新增接口无破坏性。**跨后端契约（写入 graphics spec）：**
1. args 结构体布局 = VK/D3D12 共同布局，engine 侧结构体是唯一权威定义，
   compute shader 按此布局写入；
2. **indirect draw 的 `firstInstance` 约定恒为 0**。DX12 下 `SV_InstanceID` 从 0 起且
   拿不到 StartInstanceLocation（SM6.8 以下），Vulkan 经 DXC 也会减去 base——
   跨后端唯一安全语义就是 0 起。实例基址/偏移一律走 constant buffer 或
   args 外的 storage buffer 传递，禁止依赖 firstInstance 进 shader；
3. args buffer 在 indirect 消费前必须处于 `IndirectArgs` 状态（既有 barrier 语义，
   自测链路即验证此转换）。

### Backend impact
双后端等价实现，同一自测链路两边跑。Vulkan 为直通；DX12 的 command signature
为固定三种（不含 root constant 变更），无逐 draw 状态切换。

### Performance
无既有路径变化（纯新增 API，零调用点变更）。自测默认关闭。

## Verification plan
| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 双后端构建 | 编译 | `build_editor.bat Debug` / `build_sandbox.bat Debug` |
| 单测 | 回归 | `RunTests.bat Debug` |
| 架构边界 | include 红线 | `RunArchGate.bat` |
| 自测链路 | 新 API 全链（compute 写 args → barrier → indirect draw） | `run.bat sandbox <rhi> Debug --rhi-selftest-indirect --smoke-test-seconds=5`，双后端各跑，日志见 PASS 标记 |
| 渲染门禁 | 无回归（默认关闭，画面不应变） | `RunRenderGate.bat` |
| 软渲染兼容 | WARP / lavapipe 支持确认 | 本地 WARP 跑自测；lavapipe 侧 push 后 CI 冒烟观察 |

## Task breakdown
1. `RHICommon.h` 参数结构体 + `CommandBuffer.h` 三接口 —— 编译过
2. Vulkan 实现（直通）—— vulkan 自测 PASS
3. DX12 实现（signature 缓存 + ExecuteIndirect）—— dx12 自测 PASS
4. 自测链路 + CLI 开关 —— 双后端 PASS、RenderGate 三项 PASS
5. graphics spec 回写（新接口 + 三条契约）

## Risks
| Risk | Mitigation |
| --- | --- |
| 零真实调用点的 API 设计偏差 | 自测链路即首个调用点；紧接着的粒子 SDD 是首个真实消费者，发现偏差在粒子 SDD 内修正接口（此时仍无第三方调用点，改动便宜） |
| DX12 signature 缓存生命周期管理不当 | 挂设备级缓存，随 context 销毁；三种固定 signature 无动态增长 |
| firstInstance 契约被未来代码违反 | 契约写入 graphics spec；粒子/植被 SDD 引用 |

## Open questions
- 无
