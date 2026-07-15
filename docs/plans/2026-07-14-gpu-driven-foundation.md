# GPU-driven Foundation Implementation Plan

**Status:** Gate B Done；Phase 1 Task 1 可开始。稳定工具提交 `231831f1de7930df9ccc0b23dc6565ff1a6c6d0c` 的三轮候选、90% sample-time coverage、八项 spread、受保护 report import 与唯一 non-bless COMPARED 已全部通过；批准 baseline SHA-256 为 `49D3FCCB0C068D0A90E5D2BAE667A5FDA3EB6476E6B444885F0DC103716A4659`。后续任务仍须按本计划逐项 TDD、验证和聚焦提交。

> 实施时使用 executing-plans + test-driven-development；每个任务严格 RED → GREEN → focused/full verification → 双路只读审查。计划不授权直接编辑 perf baseline 或 render golden。

**Goal:** 在不实现生产植被、GPU culling、HZB、HLOD 或 SpeedTree runtime 的前提下，为 Vulkan/DX12 建立可复用的 RenderGraph storage-buffer 生命周期、显式 indexed indirect Function 契约，以及 generation-safe GPU-driven instance page 底座。

**Architecture:** RenderGraph 继续保持 texture ref 与 buffer ref 类型分离；compiler 对 buffer 独立生成 dependency、culling、lifetime、cache identity 和 barrier plan，executor 只在 render pass 外解析 Function-level buffer transition 并提交。Renderer/RenderDevice 复用现有 StorageBuffer 和 RHI indirect API，不新增 Graphics virtual method。GPU-driven foundation 只冻结领域无关的数据/header/回收契约，不冻结植被 payload 位布局。

**Tech stack:** C++17、RenderGraph、Vulkan/DX12、Premake5/MSBuild、doctest、PowerShell PerfGate、RenderGate。

---

## Preconditions and hard scope guard

- 设计真源：
  - docs/sdd/SDD-2026-07-13-gpu-driven-foundation.md
  - docs/adr/ADR-2026-07-13-gpu-driven-instance-runtime.md
  - docs/sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md
- Phase 0 exact integration point：61c4a02b1398c3ff1230a7c95d135d4f9d4f0867。
- 当前开发分支 codex/gpu-driven-foundation 从上述 SHA 直接叠放；Phase 1 最终集成前必须先合入 Phase 0，或让 PR 明确以 Phase 0 分支为 base。禁止把 Phase 1 rebase 到不含 Phase 0 的 main。
- CPU 起点证据：
  - generate_vs2022.bat：PASS。
  - RunTests.bat Debug：136/136 cases、1937/1937 assertions。
- 已知非本任务 dirty：
  - project/thirdparty/tracy/tracy-csvexport.exe
  - project/thirdparty/tracy/tracy-profiler.exe
  两文件不得 stage、reset、改写或混入任何提交。
- Phase 0 candidate 是 RTX 5060、2560x1440、Release、全管线、无植被内容的回归锚点，不是 300 FPS 目标：
  - Vulkan CPU avg/p95 14.4149/15.4460 ms，GPU.Frame avg/p95 14.3884/15.0173 ms。
  - DX12 CPU avg/p95 14.7192/16.6116 ms，GPU.Frame avg/p95 14.5529/14.9043 ms。
  - 当前整帧约 69 FPS，距 3.33 ms / 300 FPS 约 4.3 倍；AO 单项约 11.1 ms。
- Task 0 只补齐性能比较工具和采样证据。Task 1 至 Task 9 必须等待 baseline 水位、阈值和 bless 的第二次明确批准。
- 禁止直接编辑：
  - tools/perf/perf_gate_baselines.json
  - tools/render/goldens/
- 禁止在本阶段新增：
  - Vegetation asset/brush/baker/streaming。
  - grass/tree production renderer、wind、HZB、HLOD。
  - SurfaceGPUDriven shader family。
  - bindless、mesh shader、indirect-count、多 queue、async compute。
  - RenderGraph buffer CopySrc/CopyDst。
  - 新第三方依赖或新 RHI virtual API。
- 新测试 cpp 由 tests/premake5.lua 的递归 glob 纳入；每次首次增加 cpp 后运行 generate_vs2022.bat，不修改 tests/premake5.lua。
- 所有 GPU、validation、RenderGate、PerfGate 运行前先协调本机独占窗口；不得终止其他 worktree 或用户进程。

## Approval gates

### Gate A：批准比较契约

批准后只允许执行 Task 0 的工具 TDD、三轮候选采样和报告整理；仍不 bless，不改 Phase 1 生产代码。

建议阈值使用相对百分比与绝对噪声地板的较大值：

| Metric | WARN threshold |
| --- | --- |
| CPU avg | max(5%, 0.25 ms) |
| CPU p95 | max(8%, 0.50 ms) |
| CPU p99 | max(12%, 1.00 ms) |
| Private bytes | max(5%, 128 MiB) |
| Engine heap | max(10%, 1 MiB) |
| Draw calls | 0 |
| GPU.Frame avg | max(5%, 0.25 ms) |
| GPU.Frame p95 | max(8%, 0.50 ms) |
| Pass baseline avg >= 0.5 ms | avg max(8%, 0.10 ms)，p95 max(12%, 0.20 ms) |
| Pass baseline avg >= 0.1 ms and < 0.5 ms | avg max(15%, 0.05 ms)，p95 max(20%, 0.10 ms) |
| Pass baseline avg < 0.1 ms | avg +0.03 ms，p95 +0.05 ms |

比较规则：

- GPU 11 项 required metrics 的 avg/p95 全部参与比较；不能只写 baseline 而不读回来比较。
- threshold 超限仍沿用 PerfGate WARN；但 Phase 1 的无回归验收要求零未解释 WARN。任何例外必须单独交用户裁定。
- adapter、driver、OS build、target、backend、configuration 与 workload contract 必须可归因。
- workload fingerprint 使用规范化字段生成：scene 规范路径与内容 SHA-256、extent、fixed_camera、vsync、validation、frame_cap、required GPU metric 排序集合；任一字段变化都不能复用旧 baseline。
- adapter/driver/OS/workload fingerprint 不匹配时标记 NOT_COMPARABLE 并使固定 profile 失败，不允许把异机或异 workload 数据静默标成 COMPARED。
- source SHA 与 baseline source SHA 都写入报告用于归因，但二者不要求相等；否则无法比较新提交。
- GPU.Frame 只使用专用 Frame 阈值，明确排除于 pass-tier；其余十项 required metric 才按 baseline avg 选择 pass tier。
- 三轮同机、同驱动、空闲环境候选必须背靠背采样，并逐 target+backend 独立计算 spread。CPU avg/GPU.Frame avg 的 max-min spread 不得超过 `max(3%, 0.15 ms)`；CPU p95 不得超过与其回归门禁一致的 `max(8%, 0.50 ms)`；GPU.Frame p95 不得超过 `max(5%, 0.30 ms)`。超出则停机调查，不通过放宽其他阈值掩盖噪声。

### Gate B：批准实际水位并 bless

Task 0 提交三轮报告、spread、硬件/驱动/OS 归因和建议水位后暂停。只有用户再次明确批准，才运行：

    RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release -BlessBaseline

bless 本身是一轮 fresh 采样，必须落在已批准三轮包络内；否则作废并重新请求裁定。Gate B 完成前 Task 1 不得开始。

#### Gate A candidate evidence（2026-07-14）

- 比较工具 commit：`4499d530576e572bd6c2da08dfca4329578b5b7d`（`feat(perf): compare gpu timing regression baselines`）。
- 三轮报告：
  - `Intermediate/test-reports/perf-gate/20260714-185700-726-56648-6dce638a`
  - `Intermediate/test-reports/perf-gate/20260714-190037-395-58412-3dc8b4d7`
  - `Intermediate/test-reports/perf-gate/20260714-190243-430-47176-996b7dc8`
- 三轮均为 Sandbox / Release / 2560x1440 / fixed camera / vsync off / validation off；Vulkan、DX12 共 6 条记录全部 `PASS`、`baseline_status=MISSING`，Failures/Warnings 均为 0。
- 每条记录都有 11/11 required GPU metric，GPU 总 coverage 与逐 metric 最小 coverage 均为 1.0；12 份 fresh runner log 的拒绝词命中为 0，Job Object cleanup 与有效根进程退出均已确认。
- 归因固定：NVIDIA GeForce RTX 5060；Vulkan driver `NVIDIA 580.97`；DX12 driver `32.0.15.8097`；OS build `19045.6456`；source SHA `4499d530576e572bd6c2da08dfca4329578b5b7d`；workload fingerprint `02cec7deddda8cdd7a83c95c52347a7ecab229363e30ab6632756b27569e9b6e`。
- scene SHA-256 为 `4686F78ABA32E48FB6D3AB1D2BE157C6E0DD11029561429F77C69E1D104F8BEF`；采样后 Engine.ini SHA-256 仍为 `FF5E59BD907F3A5C0CCCFB8C1743AC472B7F68ADFD1AC8FE78AD9FE9C35AE645`。

| Backend | Metric | Three-run values (ms) | max-min spread | Allowed | Result |
| --- | --- | --- | ---: | ---: | --- |
| Vulkan | CPU avg | 16.0890 / 15.9581 / 15.9367 | 0.1523 | 0.4781 | PASS |
| Vulkan | CPU p95 | 17.5086 / 17.4151 / 17.3416 | 0.1670 | 0.8671 | PASS |
| Vulkan | GPU.Frame avg | 15.8021 / 15.6927 / 15.6576 | 0.1446 | 0.4697 | PASS |
| Vulkan | GPU.Frame p95 | 16.8609 / 16.7432 / 16.5968 | 0.2641 | 0.8298 | PASS |
| DX12 | CPU avg | 15.2167 / 15.2160 / 15.1443 | 0.0724 | 0.4543 | PASS |
| DX12 | CPU p95 | 16.6212 / 16.6729 / 16.6780 | 0.0568 | 0.8311 | PASS |
| DX12 | GPU.Frame avg | 14.8446 / 14.8429 / 14.8206 | 0.0241 | 0.4446 | PASS |
| DX12 | GPU.Frame p95 | 15.8130 / 15.8110 / 15.7640 | 0.0491 | 0.7882 | PASS |

建议 Gate B 批准以上述三轮包络为 fresh bless 的接受范围。中心参考值取三轮中位数：Vulkan CPU avg/p95 `15.9581/17.4151 ms`、GPU.Frame avg/p95 `15.6927/16.7432 ms`；DX12 CPU avg/p95 `15.2160/16.6729 ms`、GPU.Frame avg/p95 `14.8429/15.8110 ms`。bless 后仍必须立即运行一次非 bless profile，要求 `baseline_status=COMPARED`、coverage 合同满足且无未解释 WARN/FAIL；否则 Gate B 不成立。

#### Gate B attempt 1（invalid，已回滚）

- 用户批准后运行 fresh `-BlessBaseline`，报告为 `Intermediate/test-reports/perf-gate/20260714-192602-500-64868-18dd7cdb`；命令 exit 0 / PASS，coverage 1.0，Failures/Warnings 与日志拒绝词均为 0。
- 但四项核心值没有落入批准包络：
  - Vulkan：CPU avg `14.2524` vs `[15.9367, 16.0890]`；CPU p95 `14.9387` vs `[17.3416, 17.5086]`；GPU.Frame avg `14.2864` vs `[15.6576, 15.8021]`；GPU.Frame p95 `14.7136` vs `[16.5968, 16.8609]`。
  - DX12：CPU avg `14.8988` vs `[15.1443, 15.2167]`；CPU p95 `15.8131` vs `[16.6212, 16.6780]`；GPU.Frame avg `14.8761` vs `[14.8206, 14.8446]`；GPU.Frame p95 `15.1073` vs `[15.7640, 15.8130]`。
- protected bless 输出还把 baseline 文本重写为 BOM/CRLF，造成全文件 `git diff --check` 失败；该 serializer 缺陷必须先经 CPU TDD 修复，不能手工格式化受保护 baseline。
- 按 stop-rule 未运行 non-bless comparison。baseline 已精确恢复到 pre-bless SHA-256 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214`，zero diff；Engine.ini 同样恢复，effective roots 为 0。
- 重新裁定路径已经执行：serializer 经 CPU TDD 修复并提交新的稳定 SHA，随后在同一独占时段重新采集三轮候选；只有下述新包络获批后才再次运行 fresh bless。

#### Gate B re-adjudication candidate（2026-07-14）

- serializer 修复 commit：`092979e804db9ab2d2d2a6c9b1d894a62d3ec310`（`fix(perf): write deterministic json artifacts`）。baseline 与 summary 现在共用 UTF-8 no-BOM / LF / single trailing LF writer；同一对象重复写入字节一致。
- TDD/CPU 证据：旧实现先精确 RED 为 writer 缺失；GREEN 后 `RunPerfGate.ps1 -SelfTest`、`TestRunPerfGate.ps1`、真实 batch DryRun、AIDevDoctor、`git diff --check` 全部 PASS；DryRun 的真实 `summary.json` 无 BOM/CR/尾随空白且可 round-trip。两路只读审查均 APPROVED，无 P0/P1/P2。
- 三轮报告：
  - `Intermediate/test-reports/perf-gate/20260714-194132-774-34496-f1960b01`
  - `Intermediate/test-reports/perf-gate/20260714-194335-942-65388-08dadacd`
  - `Intermediate/test-reports/perf-gate/20260714-194522-758-54704-45083482`
- 三轮均为 Sandbox / Release / 2560x1440 / fixed camera / vsync off / validation off；Vulkan、DX12 共 6 条记录全部 `PASS`、`baseline_status=MISSING`，Failures/Warnings 均为 0。
- 每条记录都有 11/11 required GPU metric，总 coverage 与逐 metric 最小 coverage 均为 1.0；12 份 fresh Engine/Application 日志拒绝词为 0，root/tree/Job cleanup 全部确认。
- 归因固定：NVIDIA GeForce RTX 5060；Vulkan driver `NVIDIA 580.97`；DX12 driver `32.0.15.8097`；OS build `19045.6456`；source SHA `092979e804db9ab2d2d2a6c9b1d894a62d3ec310`；workload fingerprint `02cec7deddda8cdd7a83c95c52347a7ecab229363e30ab6632756b27569e9b6e`。
- 三轮后 baseline 仍为 pre-bless SHA-256 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214` 且 zero diff；Engine.ini SHA-256 仍为 `FF5E59BD907F3A5C0CCCFB8C1743AC472B7F68ADFD1AC8FE78AD9FE9C35AE645`，effective roots 为 0。

| Backend | Metric | Three-run values (ms) | max-min spread | Allowed | Result |
| --- | --- | --- | ---: | ---: | --- |
| Vulkan | CPU avg | 13.7151 / 13.7746 / 13.8487 | 0.1336 | 0.4115 | PASS |
| Vulkan | CPU p95 | 14.3831 / 14.5319 / 14.5919 | 0.2088 | 0.7192 | PASS |
| Vulkan | GPU.Frame avg | 13.7956 / 13.8615 / 13.9337 | 0.1381 | 0.4139 | PASS |
| Vulkan | GPU.Frame p95 | 14.1312 / 14.2066 / 14.2782 | 0.1470 | 0.7066 | PASS |
| DX12 | CPU avg | 14.4479 / 14.4503 / 14.7599 | 0.3121 | 0.4334 | PASS |
| DX12 | CPU p95 | 15.1440 / 15.1453 / 15.3862 | 0.2422 | 0.7572 | PASS |
| DX12 | GPU.Frame avg | 14.4193 / 14.4234 / 14.7320 | 0.3128 | 0.4326 | PASS |
| DX12 | GPU.Frame p95 | 14.6684 / 14.6804 / 14.9658 | 0.2974 | 0.7334 | PASS |

建议 Gate B 重新批准以上述三轮包络为 fresh bless 接受范围。中心参考值取三轮中位数：Vulkan CPU avg/p95 `13.7746/14.5319 ms`、GPU.Frame avg/p95 `13.8615/14.2066 ms`；DX12 CPU avg/p95 `14.4503/15.1453 ms`、GPU.Frame avg/p95 `14.4234/14.6804 ms`。获批后仍只运行 fresh bless，再立即运行一次非 bless profile；要求 fresh bless 落在上述包络，随后 `baseline_status=COMPARED`、coverage 合同满足且无未解释 WARN/FAIL，否则 Gate B 仍不成立。

#### Gate B attempt 2（invalid，已回滚）

- 用户重新批准新包络后运行 fresh `-BlessBaseline`，报告为 `Intermediate/test-reports/perf-gate/20260714-201432-187-55088-bcff39e1`；命令 exit 0 / PASS，coverage 1.0，11/11 metrics，Failures/Warnings 与日志拒绝词均为 0，root/tree/Job cleanup 全部确认。
- serializer 修复本身有效：protected bless 输出为 UTF-8 no BOM、纯 LF、单个末尾 LF、无尾随空白，`git diff --check` PASS。
- 但 Vulkan/DX12 八项核心值全部超出批准新包络：
  - Vulkan：CPU avg `14.7527` vs `[13.7151, 13.8487]`；CPU p95 `15.5534` vs `[14.3831, 14.5919]`；GPU.Frame avg `14.6513` vs `[13.7956, 13.9337]`；GPU.Frame p95 `15.1286` vs `[14.1312, 14.2782]`。
  - DX12：CPU avg `15.0331` vs `[14.4479, 14.7599]`；CPU p95 `16.5841` vs `[15.1440, 15.3862]`；GPU.Frame avg `14.8616` vs `[14.4193, 14.7320]`；GPU.Frame p95 `15.0677` vs `[14.6684, 14.9658]`。
- 只读归因排除了 workload/内容差异：候选与 bless 除 `--perf-gate-output` 外参数逐项相同，source SHA、fingerprint、2560x1440、fixed camera、vsync/validation、233 draw calls、heap 与 private bytes 均一致；帧数下降与 GPU pass 变慢同时出现。
- 相对三轮中位数，Vulkan CPU avg/p95 `+7.10%/+7.03%`、GPU.Frame avg/p95 `+5.70%/+6.49%`；DX12 CPU avg/p95 `+4.03%/+9.50%`、GPU.Frame avg/p95 `+3.04%/+2.64%`。Vulkan 主要增加在 AO/volumetric/deferred，DX12 主要增加在 AO/TAA/tone-map/volumetric；这不是单一生产 pass 或 serializer 开销。
- 当前报告没有持久化 GPU clocks/pstate/temperature/power 或全主机 CPU/GPU utilization，无法在事后区分时钟、温度和后台调度。主机可用 `nvidia-smi`，电源方案为“高性能”，但 post-run 单点 P5 读数不能回推采样窗口状态。
- 按 stop-rule 未运行 non-bless comparison。baseline 已恢复到 SHA-256 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214` 且 zero diff；Engine.ini 同样恢复，effective roots 为 0。
- 不建议第三次重复“跨人工审批窗口 fresh bless”或放宽数值包络。用户已批准改为受保护的 report-import 模式：只接受用户已批准、SHA-256 锁定、整体及逐 run 均 PASS、cleanup/identity/coverage 完整且 source SHA 与当前工具提交一致的候选报告，由 PerfGate 工具事务性写 baseline；禁止手工编辑 baseline。先经 CPU TDD 实现并提交稳定工具 SHA，再在该 SHA 上重采三轮候选，以八项核心值到逐项中位数的归一化距离选择唯一代表报告并锁定哈希；导入后立即运行 non-bless COMPARED 验证。

#### Protected report-import implementation（2026-07-14）

- 工具合同 commit：`d59217deef42f665cb487fc5cc27630cf1e339b5`（`feat(perf): import approved baseline reports`）。
- 新入口 `-BlessBaselineFromReport <summary.json> -ExpectedReportSha256 <sha256>` 在生成新 report、build 或启动进程前早退；同一份已读取字节同时用于 SHA-256 与 JSON 解析，校验完成后克隆 baseline，并用同目录临时文件 + `File.Replace` 原子发布。
- fail-closed 合同覆盖：报告根路径、精确哈希、schema/profile/configuration/telemetry/status/baseline path、未 bless、target×backend 矩阵、当前 source SHA、workload fingerprint 与可读 workload、extent/runtime flags、required metric 集合与逐项 coverage、CPU/memory/draw 有限值、进程树/Job cleanup、zero warnings/failures。失败用例逐项证明原 baseline object 不变；成功 entry 持久化 repo-relative `source_report` 与 `source_report_sha256`。
- TDD 证据：首轮 RED 精确落在缺失 import API；第二轮 RED 分别命中 warning、identity-disabled 与原子 writer 缺失。GREEN 后 PowerShell parser、`RunPerfGate.ps1 -SelfTest`、`TestRunPerfGate.ps1`、真实 batch 临时基线 import、`RunPerfGate.bat --help`、AIDevDoctor、`git diff --check` 全部 PASS。真实 batch import 用时约 0.75 s，未创建额外 report 目录，生产 baseline SHA-256 保持 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214`。

#### Gate B protected import evidence（2026-07-14）

- 稳定工具 SHA `d59217deef42f665cb487fc5cc27630cf1e339b5` 上三轮报告：
  - `Intermediate/test-reports/perf-gate/20260714-203239-627-25236-d51726d1`
  - `Intermediate/test-reports/perf-gate/20260714-203425-489-26296-55c92ac3`
  - `Intermediate/test-reports/perf-gate/20260714-203609-120-63116-2ecb3aab`
- 6 条 run 全部 PASS/MISSING，11/11 metrics、总/逐项 coverage=1.0、Warnings/Failures/fresh 日志拒绝词为 0，cleanup 完整；baseline 与 Engine.ini 在三轮后保持原哈希。
- 八项 spread 全 PASS：Vulkan CPU avg/p95 `0.1499/0.2063 ms`、GPU.Frame avg/p95 `0.1416/0.1259 ms`；DX12 对应 `0.0409/0.0220/0.0128/0.0314 ms`，均显著低于批准门槛。
- 按八项核心值到逐项中位数的归一化相对距离，三轮得分为 `0.029624 / 0.017161 / 0.002091`；第 3 轮命中 7/8 中位数，因此唯一选中。其 `summary.json` SHA-256 为 `5aec8e5ebfbe96adecf0cfa0ab4683a6effc08d9e578e20facb6856217900c44`。
- 受保护 import exit 0，未创建 report/build/process side effect；新 baseline SHA-256 `135BA6AB17AF7F6368864F6575AF38F8DE8B61FEFBDBADE0F75289EFAEC31877`，no-BOM/LF/single-final-LF/无尾随空白与事务残留，两后端 source report/hash 与 11 metrics 完整。
- 首次 non-bless 报告 `Intermediate/test-reports/perf-gate/20260714-203847-074-50864-7070835f` 两后端均 COMPARED、coverage=1、cleanup/logs/failures 全净，但 DX12 `GPU.ToneMapAndOverlays p95` 单项 WARN：`0.236864 ms` vs baseline `0.104032 ms`，增量 `0.132832 ms` 超过 tiny-tier `0.05 ms`。
- 原始 telemetry 表明该 pass 的 p50 稳定（候选 `0.096416/0.096736/0.096352`，复验 `0.096608 ms`），三轮候选 p99 已稳定处于 `0.234048..0.248736 ms`；复验只是高延迟模态占比越过 p95 分位点（p95 `0.236864`，p99 `0.319104`），并非 pass 中心耗时整体翻倍。其余十个 metric 仅 AO/Frame 同方向 `+2.8%/+2.0%` 且在批准阈值内。按 stop-rule 已停止并释放窗口，不把该 WARN 静默排除或放宽阈值；下一步只做一次重新协调后的 confirmatory non-bless，若再次 WARN 则 Gate B 保持阻塞并提交用户裁定。
- 唯一一次 confirmatory non-bless 报告 `Intermediate/test-reports/perf-gate/20260714-204338-660-26852-d633697a` 再次得到同一结论：Vulkan PASS/COMPARED；DX12 仅 `GPU.ToneMapAndOverlays p95` WARN（`0.222336 ms` vs `0.104032 ms`，`+0.118304 ms > 0.05 ms`），其 p50 仍为 `0.096512 ms`，avg 仅 `0.105498 ms`。双后端 coverage=1、failures/log rejects=0、cleanup 完整。已按承诺停止且不再试跑；这证明 tiny-pass p95 的 bimodal quantile cliff 是可重复的统计合同问题，不能作为一次环境噪声忽略。
- 用户已批准 tiny tier fail-closed 双信号规则：阈值数值不变，baseline avg `< 0.1 ms` 的 p95 只有在 avg 同时超出 `+0.03 ms` 时才升级 WARN；原始 p95 delta 仍完整序列化。较大 pass 与 `GPU.Frame` 保持独立 avg/p95 门禁。实现先用 self-test 得到预期 RED，再最小 GREEN；因工具 source SHA 改变，旧 import baseline 作废，须在新稳定 SHA 上重采三轮、受保护导入并完成 non-bless COMPARED 后 Gate B 才成立。
- 双信号工具 commit `1a54fbe1f40bda3913e585cc0294eb65eb6da67d`（`fix(perf): corroborate tiny-pass tail warnings`）已通过 parser、self-test、`TestRunPerfGate.ps1`、AIDevDoctor ValidatePlan、help/dry-run 与 diff-check；旧 baseline 已恢复原始 SHA-256 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214` 后提交，未夹带 baseline/plan/Tracy 噪声。
- 该 SHA 首组三轮 `205606… / 205751… / 205931…` 均 PASS/MISSING、11/11 metrics、coverage=1、warnings/failures/log rejects=0、cleanup 完整，但按稳定性 stop-rule 整组作废：Vulkan CPU avg spread `0.4628 > 0.4340 ms`、CPU p95 `1.0397 > 0.7779 ms`、GPU.Frame p95 `0.8879 > 0.7578 ms`；DX12 四项与 Vulkan GPU avg 通过。漂移集中在 Vulkan `GPU.AmbientOcclusion` avg `11.01→11.21→11.37 ms`，DX12 同项反向 `11.39→11.07→11.10 ms`，两后端整帧都向此前稳定平台收敛，符合进程/驱动冷态预热而非内容变化。该组三轮只作为 preconditioning 诊断，不参与新水位；baseline/Engine.ini 均零变化并已释放窗口。下一步只允许重新采一组完整三轮验证稳定态，禁止拼接旧样本；若仍超 spread 则停止 Gate B。
- 独立新组在第 2 轮即数学上无法通过，故未浪费第 3 轮：报告 `210425… / 210605…` 均 PASS/MISSING、coverage/cleanup 完整，但 DX12 CPU p95 `16.6046 / 15.6162 ms` 已形成 `0.9884 ms` spread，超过按较小值计算的 `max(5%, 0.30)=0.7808 ms`。原始 telemetry 证明这是 CPU 队列回压相位迁移：CPU avg 仅 `14.6669→14.7875 ms`、GPU.Frame p95 仅 `14.8001→15.0221 ms`；`backend_begin_frame` avg 从 `8.5366→12.7887 ms`，`present` avg 反向从 `4.3524→0.1587 ms`，CPU p50/p95 因等待分布重排从 `16.1254/16.6046` 翻到 `14.7844/15.6162`。现候选稳定性比最终 CPU p95 回归阈值 `max(8%, 0.50 ms)` 更严，存在合同内部不一致；不再试跑，等待批准只对齐 CPU p95 候选稳定性阈值。baseline/Engine.ini 继续保持原哈希，窗口已释放。
- 用户已批准该最小对齐：CPU p95 三轮 spread 改用 `max(8%, 0.50 ms)`，GPU.Frame p95 继续使用 `max(5%, 0.30 ms)`，两项 avg 继续使用 `max(3%, 0.15 ms)`，baseline 回归比较阈值完全不变。实现必须先用同一组约 6% p95 spread 得到“CPU PASS / GPU FAIL”的 RED→GREEN，再提交新工具 SHA；此前所有候选 source SHA 均不得导入。
- 合同对齐 commit `b79b1fa535e50cc9bdb607462b055591be0d5440`（`fix(perf): align cpu tail stability contract`）按要求得到精确 RED，再以四项显式 spread 映射最小 GREEN；parser、self-test、`TestRunPerfGate.ps1`、TestAIDevDoctor、AIDevDoctor ValidatePlan、help/dry-run 与 diff-check 全部 PASS。提交仅含 `RunPerfGate.ps1`、PerfGate guide 与 tools spec，未包含 baseline、plan 或 Tracy 噪声。
- 该 SHA 首组 fresh 候选 `211611… / 211815… / 211957…` 的 6 条 run 均为 PASS/MISSING，11/11 metrics、总/逐项 coverage=1、warnings/failures/24 份 runner+Engine/Application 日志拒绝词=0，cleanup、source SHA 与 workload fingerprint 均完整；但按 stop-rule 整组作废且未导入：DX12 CPU avg 为 `14.9213 / 15.0560 / 14.5922 ms`，spread `0.4638 ms` 超过按最小值计算的 `max(3%, 0.15)=0.4378 ms`。其余七项 spread 全部通过。baseline 仍为原 SHA-256 `543EBC04B0AA2286AF61DB865297C53164B45BCF9E60A9CBEF88745400FF1214` 且 zero diff，Engine.ini 与 effective roots 已恢复。下一次只允许采集一组全新的连续三轮，禁止复用或挑选本组样本。
- 下一组在两轮后即按数学可行性提前停止，报告 `212406… / 212606…` 均为 PASS/MISSING、11/11 metrics、coverage `>=0.9957`、warnings/failures/16 份日志拒绝词=0、cleanup/identity 完整；但 Vulkan CPU avg `14.0459→14.7585 ms`、GPU.Frame avg `14.2623→14.7653 ms`，DX12 CPU avg `14.5188→15.0681 ms`、CPU p95 `18.7670→16.5848 ms` 已分别超过三轮允许 spread，任意第三值都无法修复 max-min。未跑第三轮、未导入、未 non-bless；baseline/Engine.ini 哈希与 effective roots 再次恢复。连续组在跨窗口首轮出现显著冷/热态与 CPU wait-phase 迁移，下一步必须先用既有 telemetry 做静态根因审计；禁止继续无条件重复采样或放宽回归阈值。
- 静态审计随后发现 `212406…` 的 DX12 run 虽声明 30 秒 sampling，却仅有 704 帧、CPU avg `14.518819 ms`，累计观测 frame time `10221.2486 ms`，sample-time coverage 仅 `0.3407083`；旧工具仍将其判为 PASS。用户已批准增加独立证据完整性守卫：每条 run 必须满足 `frames_sampled * CPU avg / (sample_seconds * 1000) >= 0.90`，受保护 import 必须重算并拒绝伪造/缺失字段。该修复不改变任何性能或 spread 阈值；工具 source SHA 改变后，既有候选全部失效，须重新采集完整三轮。

#### Gate B final sample-time guarded evidence（2026-07-15）

- 稳定工具 SHA `231831f1de7930df9ccc0b23dc6565ff1a6c6d0c` 上三轮报告：`20260715-103805-242-24656-c4be4b1b`、`20260715-104002-014-50172-a1fa5e8d`、`20260715-104146-636-39688-13d40c61`。6 条 run 全部 PASS/MISSING，sample-time coverage 为 `0.983263..0.986843`，11/11 metrics、总/逐项 GPU coverage=1，Warnings/Failures 与 12 份 Engine/Application fresh 日志拒绝词为 0，Job/tree cleanup、source SHA 与 workload fingerprint 完整。
- 硬件归因为 NVIDIA GeForce RTX 5060；Vulkan driver `NVIDIA 580.97`，DX12 driver `32.0.15.8097`，OS build `19045.6456`；workload fingerprint `02cec7deddda8cdd7a83c95c52347a7ecab229363e30ab6632756b27569e9b6e`。
- 八项 spread 全 PASS：Vulkan CPU avg/p95 `0.169899/0.160200 ms`、GPU.Frame avg/p95 `0.205992/0.258816 ms`，允许值分别 `0.434732/1.230944/0.433831/0.745467 ms`；DX12 对应 `0.280229/0.014000/0.318724/0.272864 ms`，允许值 `0.444095/1.324104/0.440063/0.746949 ms`。
- 三轮到八项中位数的归一化相对距离为 `0.0214892 / 0.0337282 / 0.0579328`，唯一选中第 1 轮。其 `summary.json` SHA-256 为 `aa996554c85cd41850446775a8d4400ad3cf5d3f8318436555389d2a6010300c`。
- 受保护 import exit 0，未创建 build/process/report side effect；baseline 新 SHA-256 为 `49D3FCCB0C068D0A90E5D2BAE667A5FDA3EB6476E6B444885F0DC103716A4659`，UTF-8 无 BOM、LF、单末尾 LF、无尾随空白和事务残留。两后端 entry 均记录相同 `source_report`/hash、正确 attribution/workload 与 11 GPU metrics。
- 唯一 non-bless 报告 `20260715-104438-876-35612-cc9c8e18` 整体 PASS；Vulkan/DX12 均 PASS/COMPARED，sample-time coverage `0.985962/0.983994`，总/逐项 GPU coverage=1，11/11 metrics、Warnings/Failures/日志拒绝词=0，cleanup 完整。baseline 字节未再变化，Engine.ini 恢复 `FF5E59BD…AE645`，effective roots=0。Gate B 成立。

---

## Stable contracts

### RenderGraph buffer

- RenderGraphBufferRef 与 RenderGraphTextureRef 使用独立索引类型，禁止 variant/隐式互转。
- RenderGraphBufferDesc 只描述 StorageBuffer：size、stride、shader_resource、unordered_access、indirect_args。
- 允许 access：GraphicsSRV、ComputeSRV、GraphicsUAV、ComputeUAV、IndirectArgs。
- read_buffer 只接受 SRV/IndirectArgs；write_buffer 只接受 UAV。
- transient buffer read-before-write 编译失败；external buffer read 合法。
- 写 external 或 extracted buffer 的 pass 是 culling root。
- buffer desc、external/extracted、initial access 和每个 pass usage 必须进入 cache hash 与 exact collision equality。
- texture cache 现有的 extent-insensitive 契约不在本阶段顺手修改。
- CopySrc/CopyDst 对 buffer 继续非法；full-chain readback 走 GPU 像素编码和既有 texture capture。

### Barrier and binding

- compiled buffer transition 在 compute execute 或 raster begin_pass 之前提交。
- transition 不得发生在 active render pass 内。
- graph-owned buffer 的 program SRV/UAV binding 和 indirect binding 必须与当前 pass 声明做 identity/access 核对。
- 同一 buffer 相同目标状态去重；冲突状态 fail-closed，日志包含 graph/pass/resource/binding。
- graph 外资源继续使用现有 program-binding barrier。
- executor 不直接访问 StorageBuffer::Impl，不把 Graphics/RHI 类型泄漏到 public RenderGraph API。

### Explicit indirect

- GraphicsIndirectKind：None、NonIndexed、Indexed。
- None 要求 args buffer 为空；其他 kind 要求有效且带 indirect_args usage 的 StorageBuffer。
- Indexed 必须有有效 index buffer，并在 indexed indirect command 前完成 bind。
- stride=0 解析为对应 RHI args struct 原生大小；Phase 1 为双后端一致性拒绝非原生 stride。
- draw_count > 0；offset 4-byte aligned；checked range 为 offset + native_size + (draw_count - 1) * stride，任何溢出或越界失败。
- indirect 模式要求 vertex_count、index_count、first_vertex、first_index、first_instance、vertex_offset 为 0；保留现有 GraphicsDrawDesc 的 neutral 默认 instance_count=1，避免全仓无意义迁移。
- GPU args 的 firstInstance 固定为 0；instance base 走 draw constants/storage buffer。
- Particle 只迁移为显式 NonIndexed，不在本阶段强迫其全部 buffer 进入 RenderGraph。

### GPU-driven page

- GpuDrivenPrototypeId 的 0 为 invalid。
- GpuDrivenPageHandle 比较 slot + generation；invalid slot 为 UINT32_MAX。
- slot 状态至少为 Free、Allocated、Retired；Retired 只有在调用方提供的 completed_frame_id 已覆盖 last_gpu_frame 后才可回收。
- generation 在 slot 再利用前递增；generation 即将 wrap 时永久 retire 该 slot，禁止 ABA。
- GpuDrivenInstancePageDesc 验证 count <= capacity、stride/size 非零、capacity * stride 无溢出、encoding/version 合法。
- CompressedTRS 与 Affine3x4F32 共享 page header；本阶段不冻结 CompressedTRS 位分配。
- storage ownership helper 只负责 validated desc 到 StorageBufferDesc 的转换与 shared ownership，不实现 streaming/culling。

---

## Task 0: Make the Phase 0 candidate comparable and establish an approved baseline

**Files:**

- Modify: scripts/RunPerfGate.ps1
- Modify: scripts/TestRunPerfGate.ps1
- Modify: tools/perf/perf_gate_profiles.json
- Modify: README.md
- Modify: docs/PerfGateUsageGuide.md
- Modify: docs/specs/modules/tools.md
- Modify after approval only through bless: tools/perf/perf_gate_baselines.json

- [x] Step 1: Write RED PowerShell self-tests

覆盖：

- GPU.Frame avg/p95 regression produces WARN。
- baseline avg `>= 0.1 ms` 的 required pass metric 由 avg/p95 regression 独立产生 WARN；tiny pass p95 需 avg 同时超阈。
- relative threshold 与 absolute floor 取较大值。
- tiny metric 走 absolute-only tier，未获 avg 佐证的 p95 原始超阈仍作为可审计 delta 保留。
- GPU.Frame 只命中专用阈值一次，不再进入 pass-tier。
- missing baseline metric、missing current metric、非有限数值 fail-closed。
- adapter/driver/OS mismatch yields NOT_COMPARABLE + FAIL。
- scene 内容、extent、fixed_camera、vsync、validation、frame_cap 或 required metric 集合任一 mutation 改变 workload fingerprint，并 yields NOT_COMPARABLE + FAIL。
- 三轮 spread 逐 target+backend 分组，不混合 Vulkan/DX12。
- bless output records attribution、CPU/memory/draw、11 GPU metrics and source SHA。
- telemetry-off 仍禁止 GPU baseline bless。

运行：

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -SelfTest
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1

预期 RED：现实现会写 gpu_metrics，但 Compare-RecordToBaseline 不读取 GPU baseline；VegetationFullPipeline 也没有 warn thresholds 或 attribution comparison。

- [x] Step 2: Implement the smallest comparison contract

- 为 threshold parser 增加 relative + absolute floor，不改变 Standard legacy profile 的旧字段语义。
- 为 GPU.Frame 与 pass-tier comparison 增加逐 metric delta/warning/report columns。
- baseline entry 持久化 adapter、driver、OS build、规范化 workload fingerprint/可读字段和 baseline source SHA；current report 持久化相同 workload 字段与 current source SHA。
- mismatch 不比较数值，返回 NOT_COMPARABLE。
- 不把 GPU regression 升格为工具级 FAIL；Phase 1 验收层负责将未解释 WARN 视为阻塞。

- [x] Step 3: Add the approved profile thresholds

把 Gate A 表中的 CPU/memory/draw/GPU threshold 写入 VegetationFullPipeline profile；保持 Release、2560x1440、双后端、11 metrics、coverage 0.95、vsync/validation off、fixed camera 不变。

- [x] Step 4: Verify tool changes

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -SelfTest
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
    RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release -DryRun
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan

预期：全部 PASS；DryRun 不写 baseline。

- [x] Step 5: Commit the verified comparison contract before sampling

精确提交 scripts/profile/README/PerfGate guide/tools spec；不得包含 baseline 文件或 Phase 1 C++。采样必须基于这个稳定 SHA，相关 source/config/doc 路径无 dirty；仅允许并记录已知两份 Tracy LFS checkout 噪声。

建议提交信息：

    feat(perf): compare gpu timing regression baselines

- [x] Step 6: Collect three clean candidate runs without bless

在 CPU/GPU 全独占窗口内串行运行三次：

    RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release

记录每次 report path、adapter、driver、OS build、source SHA、四项 spread 核心值及所有 WARN/FAIL。任一 FAIL、coverage < 0.95、required metric 缺失或 spread 超限即停止。

- [x] Step 7: Gate B review and protected report import

实现并提交 `-BlessBaselineFromReport <summary.json> -ExpectedReportSha256 <sha256>`：导入在任何 build/run/report side effect 前执行，报告必须位于当前 repo 的 PerfGate 报告根，且 profile/config/source/workload/矩阵/runtime/coverage/cleanup 全合同 fail-closed；失败不改 baseline，成功记录 `source_report_sha256`。在该稳定工具 SHA 上背靠背采三轮，按八项核心值到逐项中位数的归一化距离选择唯一代表报告，经用户已批准的合同锁定其哈希并导入；随后重跑一次非 bless profile，要求两后端 `baseline_status=COMPARED` 且无未解释 WARN/FAIL。

- [x] Step 8: Commit the approved baseline evidence

baseline 文件只能来自批准后的受保护 bless/report-import；同一提交只加入受保护流程输出及最终报告/文档结论，不混入 Phase 1 C++。

建议提交信息：

    perf: establish gpu-driven no-content baseline

---

## Task 1: Add the RenderGraph buffer public surface and setup validation

**Files:**

- Create: project/src/tests/Function/render_graph_buffer_tests.cpp
- Modify: project/src/engine/Function/Render/RenderGraphFwd.h
- Modify: project/src/engine/Function/Render/RenderGraphResource.h
- Modify: project/src/engine/Function/Render/RenderGraphResource.cpp
- Modify: project/src/engine/Function/Render/RenderGraphPass.h
- Modify: project/src/engine/Function/Render/RenderGraphPass.cpp
- Modify: project/src/engine/Function/Render/RenderGraphBuilder.h
- Modify: project/src/engine/Function/Render/RenderGraphBuilder.cpp
- Modify: project/src/engine/Function/Render/RenderDevice.h
- Modify: project/src/engine/Function/Render/RenderDevice.cpp

- [x] Step 1: Write focused RED cases

至少覆盖：

- RenderGraph buffer ref has an independent invalid sentinel。
- create/register/extract buffer preserve desc/name/external/extracted/initial access。
- raster/compute read_buffer accepts only SRV/IndirectArgs。
- raster/compute write_buffer accepts only matching UAV access。
- attachment、texture-only、CopySrc/CopyDst access 对 buffer 被拒绝。
- invalid ref、zero size、usage capability mismatch 被拒绝；stride=0 保持合法以兼容现有 raw/indirect args StorageBuffer。

运行：

    generate_vs2022.bat
    RunTests.bat Debug --test-case="RenderGraph buffer*"

预期 RED：RenderGraphBufferRef/Desc/Node/Usage/API 尚不存在；失败原因不得来自 premake、语法或 Tracy dirty。

- [x] Step 2: Implement only the public surface

- 新增独立 RenderGraphBufferRef。
- 新增 RenderGraphBufferDesc 和显式 StorageBufferDesc 转换。
- shader_resource/unordered_access 只约束 graph 合法访问；现有 StorageBufferDesc/RHI storage usage 不新增重复 flag，只有 indirect_args 需要映射到实际 indirect usage。
- StorageBuffer 增加只读 is_indirect_args()；external registration 由真实 buffer 的 size/stride/indirect usage 重建 desc，现有 storage usage 视为同时支持 SRV/UAV。禁止暴露 Impl/RHI handle，也不改变获批的 register_external_buffer 签名。
- 新增 IndirectArgs → AshResourceState::IndirectArgs/name mapping。
- 新增 RenderGraphBufferUsage、pass buffer_usages、两类 builder read/write_buffer。
- Builder 增加 external/test-external/create/extract/getters 和 m_buffers。
- register_external_buffer 必须保留 initial_access；不要顺手修复现有 texture initial_access 缺口。
- Task 1 不给 context 增纯虚 get_buffer，也不改 compiler/executor 签名；buffer graph 在 Task 2/3 前不可执行，避免产生无法编译的半穿线状态。

- [x] Step 3: GREEN and local review

    RunTests.bat Debug --test-case="RenderGraph buffer*"
    RunTests.bat Debug --test-case="RenderGraph GPU metric*"
    RunArchGate.bat

检查旧 texture API、GPU metric tests 和 cache test seam 无行为变化。

- [x] Step 4: Focused commit

建议提交信息：

    feat(render-graph): add storage buffer declarations

---

## Task 2: Compile buffer dependencies, culling, lifetime, barriers and cache identity

**Files:**

- Modify: project/src/engine/Function/Render/RenderGraphCompiler.h
- Modify: project/src/engine/Function/Render/RenderGraphCompiler.cpp
- Modify: project/src/engine/Function/Render/RenderGraphBuilder.cpp
- Modify: project/src/tests/Function/render_graph_buffer_tests.cpp

- [x] Step 1: Add RED compiler cases

覆盖：

- transient read-before-write fails；external read succeeds。
- write external 与 extract transient 建立 culling roots。
- dead transient producer 被裁剪；live producer-consumer chain 保留。
- buffer first/last lifetime 精确。
- UAV→SRV、UAV→IndirectArgs、IndirectArgs→SRV per-pass state/transition 精确。
- same-state duplicate usage 去重；同 pass 冲突 usage 失败。
- desc、external/extracted、initial access、usage/access、pass kind 全部影响 hash/equality。
- forced hash collision 仍执行 exact topology equality。
- buffer topology 变化造成 miss；完全相同 topology hit。
- texture-only graph 继续命中旧 cache，texture extent 继续不参与现有 identity。

预期 RED：compile result 只有 texture lifetime/transition/state，cache key 也只含 texture。

- [x] Step 2: Implement compiler data

新增：

- RenderGraphBufferLifetime。
- RenderGraphBufferTransition。
- RenderGraphPassBarrierPlan::buffer_transitions / buffer_states。
- RenderGraphCompileResult::buffer_lifetimes。

所有 compile/hash/cache 生产与 test bridge 签名显式接收 buffers。buffer dependency/culling 与 texture 共享 pass graph，但保留独立 typed arrays。

本任务同步 RenderGraphBuilder 的 compile_for_tests/compile_cached_for_tests 调用。Compiler 保留 texture-only overload 并委托空 buffer 集合，供尚未迁移的 executor/legacy tests 编译；真正 executor buffer 参数与执行留到 Task 3。

- [x] Step 3: GREEN and regression

    RunTests.bat Debug --test-case="RenderGraph buffer*"
    RunTests.bat Debug --test-case="RenderGraph*"
    RunTests.bat Debug
    RunArchGate.bat

- [x] Step 4: Focused commit

建议提交信息：

    feat(render-graph): compile buffer lifetimes and transitions

---

## Task 3: Execute graph buffer allocation and pre-pass transitions

**Files:**

- Create: project/src/engine/Function/Render/RenderGraphExecutor.h
- Modify: project/src/engine/Function/Render/RenderGraphPass.h
- Modify: project/src/engine/Function/Render/RenderGraphBuilder.h
- Modify: project/src/engine/Function/Render/RenderGraphBuilder.cpp
- Modify: project/src/engine/Function/Render/RenderGraphExecutor.cpp
- Modify: project/src/engine/Function/Render/Renderer.h
- Modify: project/src/engine/Function/Render/Renderer.cpp
- Modify: project/src/engine/Function/Render/RenderDevice.h
- Modify: project/src/engine/Function/Render/RenderDevice.cpp
- Modify: project/src/tests/Function/render_graph_buffer_tests.cpp

- [x] Step 1: Add RED executor/pool cases

覆盖：

- dead transient buffer 不分配。
- live transient buffer 只分配一次，context get_buffer 返回同一 identity。
- compatible desc 在 release 后复用；in-use buffer 不复用。
- success、compile failure、allocation failure、compute failure、raster begin failure、raster execute failure 全部释放已分配 transient buffer。
- raster end_pass failure 可被 executor 观察，且仍释放 transient buffer。
- buffer transition 在 compute execute/raster begin 之前提交。
- barrier submit failure 阻止 pass body。
- default texture-only graph 不创建 buffer、不提交 buffer barrier。
- clear_transient_storage_buffers 清空 pool。

冻结一个窄 production RenderGraphExecutionOps：

- stack-owned void* user_data + C++ function pointers，不用 std::function，不在每 pass 分配。
- acquire/release transient StorageBuffer。
- submit resolved buffer transitions。
- begin/end raster pass。
- 正常 execute 用 Renderer-backed ops；doctest 用 recorder/fault ops。
- compile 与 pass execute failure 仍分别由 invalid graph 和现有 execute lambda 注入。

新增 execute_render_graph_with_ops_for_tests，但它与正常入口调用完全相同的 execute_render_graph_core；不得暴露 StorageBuffer::Impl、复制 executor，或让测试绕过真实 cleanup 分支。该 seam 是获批 SDD 对 all-failure cleanup 的机械验证点，不是通用渲染抽象。

- [x] Step 2: Implement pool and Function-level transition submission

- Renderer 公开 graph executor 所需的 acquire/release/clear transient storage buffer facade。
- 两类 RenderGraph context 在本任务新增 get_buffer(ref)，concrete RasterContext/ComputeContext 同步实现；execute_render_graph 及 test bridge 从此显式接收 buffers。
- 新 RenderGraphExecutor.h 只声明 Function-internal resolved transition、RenderGraphExecutionOps 与窄 test bridge；生产入口也先构造 Renderer-backed ops 再进入同一个 core。
- RenderDevice 按现有 transient render-target pool 模式持有实际 buffer pool。
- 新增 Function-level resolved buffer transition；RenderDevice 内部把 StorageBuffer 转为 RHI barrier。
- 修正当前无效的 friend class RenderGraphExecutor 假设：使用明确的内部 facade 或精确 free-function friend，不把 submit_graph_resource_barriers 泛化为公共游戏 API。
- Executor 为 live transient 分配，在所有出口统一 cleanup。
- 每个 live pass 在开始实际工作前提交 compiled buffer plan；raster transition 必须早于 begin_pass。
- GraphicsPassContext::end() 与 Renderer::end_active_pass() 返回真实 pass completion 结果；析构/move cleanup 可忽略返回值，但 executor 必须检查，并让 end_pass failure 进入统一 fail_execution cleanup。
- Copy access 不加入实现。

- [x] Step 3: GREEN and CPU gates

    RunTests.bat Debug --test-case="RenderGraph buffer*"
    RunTests.bat Debug
    RunArchGate.bat
    build_editor.bat Debug
    build_sandbox.bat Debug

- [x] Step 4: Short dual-backend validation checkpoint

先申请 GPU 窗口，再用一个默认无 graph-buffer 的短 smoke 验证 disabled-path 零 validation/debug-layer error。此 checkpoint 不运行 bless。

- [x] Step 5: Focused commit

建议提交信息：

    feat(render-graph): execute transient buffer barriers

---

## Task 4: Validate graph-owned program bindings against pass declarations

**Files:**

- Modify: project/src/engine/Function/Render/RenderGraphPass.h
- Modify: project/src/engine/Function/Render/RenderGraphExecutor.cpp
- Modify: project/src/engine/Function/Render/Renderer.h
- Modify: project/src/engine/Function/Render/Renderer.cpp
- Modify: project/src/engine/Function/Render/RenderDevice.h
- Modify: project/src/engine/Function/Render/RenderDevice.cpp
- Modify: project/src/tests/Function/render_graph_buffer_tests.cpp

- [x] Step 1: Add RED identity/access cases

覆盖 graphics 与 compute 两条真实调用路径：

- declared GraphicsSRV bound as graphics SRV succeeds。
- declared ComputeUAV bound as compute UAV succeeds。
- graph-owned buffer 未声明却被 program binding 使用，失败。
- 声明 SRV 却绑定 UAV，失败；声明 UAV 却绑定 SRV，失败。
- pass 声明 buffer A，program 同名 binding 指向 buffer B，失败。
- indirect args 必须声明 IndirectArgs。
- same buffer/same target state 去重。
- graph 外 program buffer 保持既有自动 barrier。
- raster draw 入队后若 program binding 被修改，必须按 end_active_pass 时的最终 binding 重新核对并 fail-closed，禁止 TOCTOU。
- 错误包含 pass、resource、binding 与 expected/actual access。

- [x] Step 2: Implement one production validator

实现由 raster draw 与 compute dispatch 共用的 graph-owned buffer identity/access validator；这是两个真实生产调用点，不建立 test-only free function。

Context 保存当前 pass declared buffer map。compute 在即时 dispatch 点核对；raster 把 declared map 带入 GraphicsPassContext，并在 end_active_pass 的实际 barrier collection/command submission 点读取最终 GraphicsProgram bindings 后核对，不能只在 RasterContext::draw 入队时检查。已由 graph plan transition 的 buffer 不再被 program-binding collector 重复改写；未纳入 graph 的资源继续走旧路径。

- [x] Step 3: GREEN and full CPU gates

    RunTests.bat Debug --test-case="RenderGraph buffer*"
    RunTests.bat Debug
    RunArchGate.bat

- [x] Step 4: Focused commit

建议提交信息：

    feat(render-graph): validate graph buffer bindings

---

## Task 5: Add explicit non-indexed/indexed indirect draw policy

**Files:**

- Create: project/src/tests/Function/renderer_indirect_policy_tests.cpp
- Modify: project/src/engine/Function/Render/Renderer.h
- Modify: project/src/engine/Function/Render/Renderer.cpp
- Modify: project/src/engine/Function/Render/RenderDevice.h
- Modify: project/src/engine/Function/Render/RenderDevice.cpp

- [ ] Step 1: Write pure descriptor/policy RED cases

覆盖 Stable contracts 中全部 None/NonIndexed/Indexed、usage、IB、neutral direct state、count、stride、alignment、checked overflow/range 规则。

特别覆盖：

- stride=0 分别解析为 AshDrawIndirectArgs / AshDrawIndexedIndirectArgs。
- custom stride 在 Phase 1 双后端合同中被拒绝。
- draw_count > 1 range 使用最后一条 command 末尾，不只检查第一条。
- Indexed 在无 index buffer 时失败。
- legacy neutral instance_count=1 不把 indirect desc 误判冲突。
- indirect instance_count 必须等于 neutral 默认 1；instance_count=2 明确拒绝。

运行：

    generate_vs2022.bat
    RunTests.bat Debug --test-case="Renderer indirect*"

预期 RED：explicit kind/count/stride 与 indexed Function path 均不存在。

- [ ] Step 2: Implement shared validation and Renderer routing

- 增加 GraphicsIndirectKind 与字段。
- 增加可纯构造的 production GraphicsIndirectValidationFacts 与 GraphicsIndirectValidationResult/validator。facts 只含 resource_present/size/indirect_usage/index_present 及 draw desc 标量；Renderer/RenderDevice 两个真实路径从实际对象填充，doctest 直接构造 facts，不暴露 Impl，也不复制一份测试算法。
- pass barrier collection 和 command submission 共用同一 resolved kind/count/stride/range，避免前后不一致。
- 增加窄 operation-recorder test bridge，调用同一 production routing core，机械证明 Indexed 顺序为 bind index buffer → cmd_draw_indexed_indirect，且绝不调用 non-indexed path；bridge 不实现第二套路由。
- Indexed indirect 在 command 前 bind index buffer。
- RenderDevice 扩展 non-indexed count/stride，并新增 indexed indirect method。
- 两条方法调用既有 RHI cmd_draw_indirect / cmd_draw_indexed_indirect。
- 不修改 Graphics/CommandBuffer virtual interface 或双后端实现。

- [ ] Step 3: GREEN, full tests and backend builds

    RunTests.bat Debug --test-case="Renderer indirect*"
    RunTests.bat Debug
    RunArchGate.bat
    build_editor.bat Debug
    build_sandbox.bat Debug
    build_sandbox.bat Release

- [ ] Step 4: Focused commit

建议提交信息：

    feat(render): add explicit indexed indirect draws

---

## Task 6: Migrate Particle to explicit NonIndexed without graph migration

**Files:**

- Modify: project/src/engine/Function/Render/ParticleSystemPass.cpp
- Modify: project/src/tests/Function/particle_depth_reconstruction_tests.cpp

- [ ] Step 1: Add the smallest RED contract check

在现有 Particle source-contract doctest 中检查唯一 draw site 显式设置 GraphicsIndirectKind::NonIndexed。这里复用既有 source-contract 范式，避免为一个调用点新增 production abstraction。

预期 RED：当前只设置 args buffer/offset。

- [ ] Step 2: Set explicit NonIndexed

只添加 kind；count=1、stride=0 使用默认。不要把 Particle candidate/alive/args buffer 顺手迁入 RenderGraph。

- [ ] Step 3: CPU GREEN

    RunTests.bat Debug --test-case="*Particle*"
    RunTests.bat Debug
    RunArchGate.bat

- [ ] Step 4: Render checkpoint

申请 GPU 独占窗口后运行：

    RunRenderGate.bat

要求 Vulkan/DX12 particle golden 与 cross-backend 全 PASS，fresh log 无 validation/debug/device-lost/fatal。禁止 bless；若画面变化先停机调查。

- [ ] Step 5: Focused commit

建议提交信息：

    refactor(particles): declare non-indexed indirect kind

---

## Task 7: Add versioned GPU-driven types, page retirement and storage ownership

**Files:**

- Create: project/src/engine/Function/Render/GPUDriven/GpuDrivenTypes.h
- Create: project/src/engine/Function/Render/GPUDriven/GpuDrivenPageAllocator.h
- Create: project/src/engine/Function/Render/GPUDriven/GpuDrivenPageAllocator.cpp
- Create: project/src/engine/Function/Render/GPUDriven/GpuDrivenInstancePageStorage.h
- Create: project/src/engine/Function/Render/GPUDriven/GpuDrivenInstancePageStorage.cpp
- Create: project/src/tests/Function/gpu_driven_foundation_tests.cpp

- [ ] Step 1: Write deterministic RED cases

覆盖：

- Prototype ID 0 invalid。
- invalid/default handle 与 slot+generation equality。
- allocate → retire 不立即复用。
- collect_completed 在 completed_frame < last_gpu_frame 时不回收。
- completion 后 generation 递增并允许复用。
- stale、double-retire、foreign/out-of-range handle 被拒绝。
- generation wrap 永久封存 slot。
- capacity exhaustion 明确失败。
- desc 拒绝 zero/overflow/count>capacity/unknown encoding/version。
- CompressedTRS 与 Affine3x4F32 保持不同 encoding，但共享 header/version/stride contract。
- storage helper 只接受 validated desc，byte size checked，创建的 buffer flags 与 shader/UAV/indirect 需求匹配。

运行：

    generate_vs2022.bat
    RunTests.bat Debug --test-case="GPU-driven foundation*"

- [ ] Step 2: Implement minimal production data

- 类型不得出现 grass、tree、SpeedTree、brush 或 terrain 字段。
- page allocator 接收调用方的 canonical completed frame ID，不依赖 Graphics fence 类型。
- slot 回收 fail-closed；不做后台线程、streaming 或 residency policy。
- InstancePageStorage 只持 desc、handle 和 shared_ptr<StorageBuffer>；不实现 culling/LOD。
- 不定义 CompressedTRS 的最终 bit allocation。

- [ ] Step 3: GREEN and architecture gates

    RunTests.bat Debug --test-case="GPU-driven foundation*"
    RunTests.bat Debug
    RunArchGate.bat
    build_editor.bat Debug
    build_sandbox.bat Debug

- [ ] Step 4: Focused commit

建议提交信息：

    feat(render): add gpu-driven page contracts

---

## Task 8: Add the Function RenderGraph compute-to-indexed-indirect full-chain self-test

**Files:**

- Create: project/src/engine/Function/Render/RenderGraphIndirectSelfTest.h
- Create: project/src/engine/Function/Render/RenderGraphIndirectSelfTest.cpp
- Create: project/src/engine/Shaders/SelfTest/RenderGraphIndirectSelfTest.hlsl
- Modify: project/src/engine/EntryPoint.h
- Modify: project/src/engine/Function/Application.h
- Modify: project/src/engine/Function/Application.cpp
- Modify: project/src/tests/Function/application_automation_tests.cpp
- Modify: project/src/tests/Function/render_graph_buffer_tests.cpp

- [ ] Step 1: Add RED lifecycle and graph-chain tests

覆盖：

- 现有 --rhi-selftest-indirect 仍先跑 raw RHI substrate，再跑 Function graph chain。
- 任一步失败传播 non-zero process result，日志区分 RHI 与 RenderGraph stage。
- self-test flag 自动启用有界 process watchdog；即使卡在 startup/raw wait/graph frame teardown，也必须在 deadline 后 hard fail，不能无限挂起。
- begin_frame 后 graph/capture/end/present 任一失败都 exactly-once 关闭 active pass/frame 并释放 transient；capture 只在 graph 成功后请求，end_frame 必须消费 request，fetch 失败后直接进入受控 runtime shutdown，禁止复用 pending capture；逐阶段 failure injection 覆盖。
- self-test 默认关闭，不给普通帧新增 pass/resource/allocation。
- graph topology 包含 external candidate、transient visible、transient args。
- compute pass：candidate SRV，visible/args UAV。
- indexed raster pass：visible SRV，args IndirectArgs，并使用 explicit Indexed。
- validation raster pass：args GraphicsSRV。

- [ ] Step 2: Implement without buffer copy

流程：

1. 建立一个 one-shot Renderer mini-frame，遵守 begin_frame → graph execute → request_back_buffer_capture → end_frame/present → finite fetch 的完整生命周期；capture 必须在 end_frame 录制 copy 之前提出。
2. compute shader 从已知 candidate 写 visible payload 和一条 indexed args，firstInstance=0。
3. indexed raster 把已知 tile 写入 output。
4. validation raster 以 GraphicsSRV 读取 args 五字段，只有精确匹配时写第二个验证 tile。
5. 使用现有 back-buffer texture capture 读取两个内缩 tile/多像素区域。
6. 任一像素、args encoding、timeout、barrier 或 validation error 失败。

不得新增 graph buffer CopySrc/CopyDst、buffer-to-buffer RHI copy 或同步 wait_idle 热路径。self-test 的有限 completion wait 只走既有 capture contract。

EntryPoint 为 indirect self-test 提供默认 120 秒 process watchdog；Application 在 self-test 完成后请求退出，不继续无限正常 loop。one-shot helper 用 frame guard 保证 begin_frame 成功后 end_frame exactly once，只有成功 end_frame 才 present。所有 matrix 命令同时带 --run-for-frames=1 作为正常路径上界，外层脚本另设 120 秒 watchdog。

- [ ] Step 3: CPU GREEN and builds

    RunTests.bat Debug --test-case="RenderGraph buffer*"
    RunTests.bat Debug --test-case="*indirect self-test*"
    RunTests.bat Debug
    RunArchGate.bat
    build_sandbox.bat Debug
    build_sandbox.bat Release

- [ ] Step 4: Dual-backend self-test matrix

申请 GPU 独占窗口，严格串行运行 Vulkan → DX12：

- Debug，validation on。
- Release，validation on。
- 每格使用精确 bounded CLI：--rhi-selftest-indirect --run-for-frames=1，外层 120 秒 watchdog；不得依赖人工关闭窗口。
- 每格要求 raw RHI PASS、Function RenderGraph PASS、clean exit、fresh log generic error/critical 与 validation/debug/device-lost/fatal 为 0。
- 若环境已有受支持的软件 adapter 选择机制，再补 WARP/lavapipe；当前仓库没有 adapter selector，因此不得为满足该项临时扩 RHI。缺失时记录为环境能力缺口并提交用户裁定，不能虚报已跑。

- [ ] Step 5: Focused commit

建议提交信息：

    test(render): cover graph indexed indirect full chain

---

## Task 9: Final verification, specs and phase closure

**Files:**

- Modify: README.md
- Modify: docs/VERIFY.md
- Modify: docs/specs/modules/render-graph.md
- Modify: docs/specs/modules/render.md
- Modify: docs/specs/modules/graphics.md
- Modify: docs/specs/modules/tools.md only if Task 0 conclusions changed
- Modify: docs/sdd/SDD-2026-07-13-gpu-driven-foundation.md
- Modify: docs/plans/2026-07-14-gpu-driven-foundation.md
- Modify: docs/plans/README.md

- [ ] Step 1: Fresh CPU evidence

    generate_vs2022.bat
    RunTests.bat Debug
    RunArchGate.bat
    build_editor.bat Debug
    build_sandbox.bat Debug
    build_sandbox.bat Release
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan

记录 exact cases/assertions、legacy WARN、build exits 和 git diff --check。

- [ ] Step 2: Readiness and validation

申请 GPU 窗口后运行：

    run.bat all Debug --smoke-test-seconds=120

随后按项目既有 validation 配置分别运行 Vulkan/DX12 self-test 和短 Sandbox smoke。finally 恢复 Engine.ini、EditorSettings.json、ViewportLayout.json、imgui.ini，并用运行前 SHA-256 精确审计。

- [ ] Step 3: RenderGate

    RunRenderGate.bat

要求双后端 golden/cross 全 PASS、无 bless、fresh logs clean。

- [ ] Step 4: Performance gates

取得 CPU/GPU 全独占后运行：

    RunPerfGate.bat -Profile Standard
    RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release

要求：

- Standard 全矩阵 PASS。
- VegetationFullPipeline baseline_status=COMPARED。
- coverage/per-metric coverage >= 0.95。
- invalid/unresolved=0。
- required GPU metrics 全部存在。
- 零 FAIL，零未解释 WARN。
- no-content disabled path 不新增 graph buffer allocation/pass/draw。
- 不 bless。

- [ ] Step 5: Two independent static reviews

审查重点：

- graph buffer access legality、initial state、culling/lifetime/cache exact equality。
- all-success/all-failure transient cleanup。
- barrier outside active pass。
- graph/program identity conflict fail-closed。
- checked arithmetic、DX12/Vulkan stride parity、indexed IB bind order。
- page generation/retirement ABA safety。
- default scene zero-cost path。
- no Graphics virtual/API expansion。
- no vegetation-specific field leaked into GPUDriven。

任何 P0/P1/P2 finding 先补 RED 再修，之后按影响重跑 gates。

- [ ] Step 6: Write back stable contracts

- render-graph.md：buffer public API、allowed access、dependency/culling/lifetime/cache/barrier/binding validation。
- render.md：explicit indirect policy、Particle migration、GPUDriven experimental foundation。
- graphics.md：确认复用既有双后端 indirect 和 StorageBuffer state，无新 virtual API。
- README.md / docs/VERIFY.md：同步新增 full-chain self-test 命令与 Phase 1 双后端验证矩阵。
- Phase 1 SDD 标 Done，记录实际验证、baseline report 和未完成项。
- plan 全部 checkbox 完成后移到 Archived；后续生产 grass/tree 进入新的 S2 设计，不自动开始。

- [ ] Step 7: Final selective commit and integration handoff

确认 staged paths 只包含本计划文件；Tracy 两 exe 永不暂存。Phase 0 先合入，再 rebase/retarget Phase 1。最终提交/PR 前重跑 git diff --check 和 branch ancestry audit。

建议文档提交信息：

    docs(render): close gpu-driven foundation phase

---

## Stop rules

- Gate A 或 Gate B 未批准：停止。
- Phase 0 未成为一致集成点：允许只在当前 stacked worktree 继续已批准开发，禁止创建面向 main 的错误 PR。
- RED 失败原因不是待实现 contract：停止并修复测试纯度。
- 单后端通过、另一后端失败：停止，不做 backend-specific 绕过。
- validation/debug-layer 任一 error：视同 bug。
- RenderGate FAIL：停止；不得 bless，除非用户先确认预期画面变化。
- PerfGate FAIL：禁止提交。
- PerfGate WARN：提交用户裁定；不得用放宽阈值掩盖。
- baseline candidate spread 超限或 hardware attribution 不可比：禁止 bless。
- 需要新增 Copy access、RHI virtual API、adapter selector、indirect-count 或生产 shader family：超出本计划，先写 SDD amendment 并重新批准。

## Phase 1 exit criteria

- RenderGraph external/transient StorageBuffer 的 API、compiler、executor、cache、barrier 和 binding validation 有确定性测试。
- Function explicit non-indexed/indexed indirect 双后端等价，Particle 行为不变。
- generation-safe page retirement、layout validation 和 storage ownership helper 可供后续 grass/tree 复用。
- raw RHI + Function RenderGraph full-chain self-test 双后端 Debug/Release 与 validation 通过。
- RunTests、ArchGate、builds、readiness、RenderGate、Standard 和 VegetationFullPipeline 全绿。
- baseline 可归因且实际参与 GPU regression compare；未把约 69 FPS 候选描述为 300 FPS 达标。
- specs/SDD/plan 状态同步，工作树除既有 Tracy 噪声外无未解释 dirty。
