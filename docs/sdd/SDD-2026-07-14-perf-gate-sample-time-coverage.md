# Mini SDD: PerfGate 采样时间覆盖率守卫

**Status:** Done（2026-07-15）

## Goal

PerfGate 必须拒绝没有连续覆盖配置采样窗口的报告。固定 profile 的 schema v2 telemetry 由 Application 提供 wall-clock 证据，对每条 run 计算：

`sample_time_coverage = sample_observed_seconds / profile.sample_seconds`

只有 coverage 不小于 `0.90`、最大相邻 gap 不超过 `0.25 s`、平均采样率不低于 `30 Hz`，且 `span <= (frames_sampled - 1) * max_gap` 时，run 才允许 PASS、进入候选 spread 或受保护 report import。旧的 `frames * renderer CPU avg` 只能反映 renderer 内计时，不能证明主循环连续运行，现已废弃为 v2 完整性证据。

## Non-goals

- 不放宽 CPU/GPU 回归阈值或三轮 spread 阈值。
- 不放宽采样时长、预热时长、workload fingerprint 或性能阈值。
- 不把 renderer CPU avg 当成 whole-loop latency；采样完整性由独立 wall-clock span/gap/rate 约束。
- 不修改 perf baseline，不开始 GPU-driven Phase 1 生产代码。

## Files

- `scripts/RunPerfGate.ps1`
- `scripts/TestRunPerfGate.ps1`
- `docs/PerfGateUsageGuide.md`
- `docs/specs/modules/tools.md`
- `docs/plans/2026-07-14-gpu-driven-foundation.md`
- 本 Mini SDD

## Approach

1. `PerfGateController` 在 sampling 窗口记录首末 wall-clock 时间与最大相邻 sample gap，并输出 schema v2 的 `sample_observed_seconds` / `sample_max_gap_seconds`；schema v1 无条件保留 legacy 展示路径。
2. runner 同时校验 coverage、最大 gap、`frames / sample_seconds >= 30 Hz` 和 timeline 数学一致性；任一失败都使整体 PerfGate FAIL。
3. live report 从一次 `ReadAllBytes` snapshot 同时解析 raw JSON 和计算 SHA，summary 持久化 report-relative raw 路径与 digest。
4. 受保护 import 对 summary/raw 分别使用同一-byte parse+hash，要求 artifact 顶层及 `workload/runtime/CPU/memory/render stats/GPU/metrics/backend info/逐 metric` 容器为原生 JSON object，并严格检查 JSON 原生 string/number/integer/boolean scalar；单元素数组不能伪装成标量或对象。导入复用 live canonical validator 从 raw 重建所有 baseline 数值、GPU metric 与 runtime/identity 字段；summary 与 canonical raw 逐项一致后才克隆并发布 baseline。
5. summary/raw 路径逐组件拒绝 `ReparsePoint`；同权限本地恶意并发目录替换不在威胁模型内，当前实现不宣称 handle-pinned path security。
6. TDD 覆盖两端点跨窗但中间大 gap、约 4 FPS 连续样本、timeline 自相矛盾、same-byte snapshot、顶层/嵌套对象及字符串/整数/浮点/布尔的单元素数组伪装、summary 一致伪造、raw digest 与 reparse 逃逸。

## Verification

```bat
powershell -NoProfile -Command "[void][scriptblock]::Create((Get-Content -Raw scripts/RunPerfGate.ps1))"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/RunPerfGate.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestRunPerfGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/TestAIDevDoctor.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
RunPerfGate.bat --help
RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release -DryRun
```

最终工具提交为 `0ef8da1efa24bd85107681047907d7790c59c4a0`，脚本 SHA-256 为 `89CC3FC984C6E0324E1402C704C0E5B310846BD98B3B87C7D8D489E0DB5A3C50`。`RunPerfGate.ps1 -SelfTest`、`TestRunPerfGate.ps1`、`git diff --check` 与两路静态复审均通过。exact-SHA 最终门禁：Standard 报告 `20260715-151351-919-17136-479faf75` 四组合 PASS；最终有效 VegetationFullPipeline Release non-bless 报告 `20260715-153105-257-3488-3a9c623c` 双后端 PASS/COMPARED，11/11 required metrics、总/逐项 GPU coverage=1、sample-time coverage 为 `0.999322 / 0.999452`，最大 gap 为 `0.01955 / 0.02968 s`，warnings/failures 均为 0。中间 WARN 与窗口最小化导致的 coverage FAIL 均按 stop-rule 停止并排除。baseline SHA-256 保持 `49D3FCCB0C068D0A90E5D2BAE667A5FDA3EB6476E6B444885F0DC103716A4659`，本修复没有 import 或 bless。

## Risk / rollback

- `0.90`、`0.25 s` 与 `30 Hz` 是证据完整性下限，不是 CPU/GPU 回归阈值。
- 若某 profile 合法使用低帧率 frame cap，应在 profile 设计阶段调整完整性合同并重新评审；禁止在候选或 import 阶段绕过守卫。
- 回滚只需撤销工具、测试与文档提交；baseline 在修复验证和新候选批准前保持原字节。
