# SDD-2026-07-07-dx12-mailbox-present-tearing: DX12 MAILBOX present 语义修正（去除误加的 ALLOW_TEARING）

级别：S2（`Graphics/` 高危路径，DX12 后端 swapchain）

## Status

Done（2026-07-06，结论部分被后续取证修正）

- present 语义修正本身成立并保留（MAILBOX 不得撕裂，`present_flags=0x0` 日志实锤）。
- 但"抖动根因是撕裂"的判断**被推翻**：修正后抖动仍在。RenderDoc 取证定位真根因为
  Function 层 TAA jitter 对共享 `VisibleRenderFrame` 重复施加（prepare/submit 节奏
  不同步时同帧重渲、jitter 累加 2×），见
  [SDD-2026-07-07-taa-jitter-double-apply](SDD-2026-07-07-taa-jitter-double-apply.md)。撕裂只是把 2× jitter 帧的差异
  拼上屏的放大器。本 SDD 的 Context 证据矩阵解释：VSync=true 时 DXGI 阻塞渲染线程、
  渲染 fps 被压到逻辑 fps 之下故无同帧重渲，而非撕裂消失所致。

## Context

任务 #23：TAA 开启时 DX12 交互态整画面抖动，Vulkan 无此现象（SDD-2026-07-07-debug-draw-thickness 验证期发现，
已 A/B 确认为既有问题）。排查证据矩阵（2026-07-05，全部实测）：

| 配置 | 结果 |
| --- | --- |
| DX12 + TAA + VSync=false（sync=0 + ALLOW_TEARING） | 抖 |
| DX12 + TAA + VSync=true（FIFO） | 稳 |
| Vulkan + TAA + MAILBOX（日志实锤选中） | 稳 |
| DX12 无 TAA + VSync=false | 稳 |
| MotionVectors debug 视图：DX12 明显抖 / Vulkan 稳定纯黑（同一份 shader） | — |

结论：TAA/运动向量/jitter 补偿数学双后端共享且正确（收敛区残差 ≈ (1-blend)×jitter
≈ 0.05px；`use_history=false` 像素如天空为全振幅，属设计内）。**根因在呈现端**：
`DX12Swapchain.cpp` 把请求的 MAILBOX 实现成 `sync_interval=0 +
DXGI_PRESENT_ALLOW_TEARING`（日志 `present_mode=MAILBOX, present_flags=0x200`），
这是 IMMEDIATE 的语义。真正的 MAILBOX 绝不撕裂。撕裂把 TAA 相邻帧的亚像素差异
拼在同一屏上且撕裂缝逐帧乱跳，被感知为整画面抖动；无 TAA 时相邻帧逐位相同，
撕裂不可见，故此 bug 长期潜伏。

## Goals

- DX12 MAILBOX 分支实现真 mailbox 语义：flip model `sync_interval=0`、**不带**
  `DXGI_PRESENT_ALLOW_TEARING`（DXGI 弃旧帧取最新完整帧，无撕裂、低延迟）。
- DX12 + TAA + VSync=false 交互态不再抖动（与 Vulkan 行为对齐）。
- 保留 VulkanSwapchain 新增的 selected present_mode 日志（排查期加入，双后端
  可观测性对齐）。

## Non-goals

- 不改 IMMEDIATE 分支：撕裂是其合同语义（最低延迟），保留 `ALLOW_TEARING`。
- 不改 `Application` 的 present mode 请求顺序（`{MAILBOX, IMMEDIATE, FIFO}`）。
- 不新增 Engine.ini 配置项（如显式选 IMMEDIATE 的开关，另行立案）。
- 不动 TAA 任何代码（数学已排除嫌疑）。

## Current implementation

- Entry points：`Application.cpp:159-175` 按 `VSync` 生成 present mode 请求列表 →
  `DX12Swapchain::init`（`DX12Swapchain.cpp:90-125`）遍历选择。
- 现状缺陷：MAILBOX 与 IMMEDIATE 两个分支代码完全相同（sync=0 + tearing 标志），
  MAILBOX 排在请求列表首位，故 DX12 实际一直在撕裂。
- swapchain 创建带 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`（能力标志，保留——
  IMMEDIATE 路径仍需要）。
- Known constraints：FLIP_DISCARD + sync=0 不带 tearing 标志时，DXGI 以最新完整帧
  替换排队帧，即 mailbox 等价实现；窗口合成与独立翻转两种路径下均无撕裂。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Graphics/DirectX12 | MAILBOX 分支 `m_presentFlags` 保持 0（删 tearing 赋值），IMMEDIATE 分支不变 | `project/src/engine/Graphics/DirectX12/DX12Swapchain.cpp` |
| Graphics/Vulkan | 保留已加入的 selected present_mode HLogInfo（无行为变化） | `project/src/engine/Graphics/Vulkan/VulkanSwapchain.cpp` |

### API / contract changes

无。RHI 接口、`AshPresentMode` 枚举、请求列表语义均不变；仅 DX12 后端对
MAILBOX 的落地实现修正为符合枚举既有语义。

### Backend impact

- DX12：VSync=false 时不再撕裂；帧率仍不封顶（Present 以新帧替换队列旧帧，
  不阻塞）。延迟相比撕裂路径略增（≤1 次合成），符合 MAILBOX 定义。
- Vulkan：仅新增一行创建期日志。

### Performance

GPU 工作量零变化（纯 present 标志位）。PerfGate 指标若含 present/帧间隔统计，
DX12 交互态数值可能轻微移动；按 `Graphics/` 高危路径规则跑全矩阵确认。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 双后端构建 | 编译 | `build_editor.bat Debug` + `build_sandbox.bat Debug` |
| 渲染门禁 | golden 必须不变（present 不影响渲染内容） | `RunRenderGate.bat` |
| 性能门禁全矩阵 | Graphics/ 高危路径要求 | `RunPerfGate.bat -Profile Standard` |
| 日志确认 | DX12 创建行应显示 `present_mode=MAILBOX, present_flags=0x0`；无 validation 报错 | 双后端各 smoke 一次，查 `product/logs` |
| 目视验证（用户） | DX12 + TAA + VSync=false 静止画面无抖动；Vulkan 行为不变 | 交互运行 |

## Task breakdown

1. 修改 MAILBOX 分支（1 行删除级 diff）→ 验收：日志 `present_flags=0x0`。
2. 双后端构建 + 门禁 → 验收：RenderGate PASS 且 golden 未变、PerfGate 无 FAIL。
3. 用户目视确认 DX12 不抖 → 验收：用户口头确认。
4. Spec 回写（同一提交）：
   - `docs/specs/features/taa.md`：已知限制中"DX12 交互态整画面抖动"条目改为
     已解决（根因 present 撕裂，指向本 SDD）；
   - `docs/specs/modules/graphics.md`：补记 DX12 present mode → sync/flags 映射
     语义（MAILBOX 不撕裂 / IMMEDIATE 撕裂 / FIFO sync=1）。

## Risks

| Risk | Mitigation |
| --- | --- |
| 个别驱动/合成路径下 sync=0 无 tearing 出现帧节奏变化（延迟或卡顿感） | 用户交互目视验证；如不可接受，回退单行即可 |
| PerfGate 帧间隔类指标漂移 | 全矩阵跑完对照阈值；WARN 时写明判断理由 |
| 回滚 | 单行 revert，无数据迁移 |

## Open questions

- 是否需要 Engine.ini 显式 present mode 覆盖项（强制 IMMEDIATE 给延迟敏感场景）？
  本 SDD 不做，倾向另行立案。
