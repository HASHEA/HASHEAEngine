# Mini SDD: PerfGate 采样时间覆盖率守卫

**Status:** Done

## Goal

PerfGate 必须拒绝没有覆盖配置采样窗口的报告。对每条 run 计算：

`sample_time_coverage = frames_sampled * cpu_frame_time_avg_ms / (sample_seconds * 1000)`

只有有限且不小于 `0.90` 的覆盖率才允许 run PASS、进入候选 spread 或被受保护 report import。当前异常证据为 30 秒 DX12 采样只有 704 帧、CPU avg 14.5188 ms，覆盖率约 `0.341`；同 SHA 正常样本约 `0.96..0.99`。

## Non-goals

- 不放宽 CPU/GPU 回归阈值或三轮 spread 阈值。
- 不改变 Application telemetry schema、采样时长、预热时长或 workload fingerprint。
- 不引入固定最小 FPS/帧数；低帧率 workload 只要累计 frame time 覆盖窗口仍可通过。
- 不修改 perf baseline，不开始 GPU-driven Phase 1 生产代码。

## Files

- `scripts/RunPerfGate.ps1`
- `scripts/TestRunPerfGate.ps1`
- `docs/PerfGateUsageGuide.md`
- `docs/specs/modules/tools.md`
- `docs/plans/2026-07-14-gpu-driven-foundation.md`
- 本 Mini SDD

## Approach

1. 在真实 telemetry 聚合边界读取并验证 `sample_seconds`、`frames_sampled` 与 CPU avg，计算观测 frame-time 总量和覆盖率。
2. 覆盖率 `< 0.90`、非有限或输入无效时给 run 添加明确 failure，并使整体 PerfGate FAIL；错误必须包含 observed/expected/ratio，便于区分性能回归与证据缺失。
3. summary run 新增 `sample_seconds`、`observed_frame_time_ms` 和 `sample_time_coverage`，保持 schema v2 的向后兼容扩展。
4. 受保护 report import 重新计算覆盖率而非只信任序列化结果；缺字段、字段不一致或覆盖不足均 fail-closed，baseline object 保持不变。
5. TDD 先锁定 `0.90` 边界通过、当前 `0.341` 异常失败、import 无法绕过；再做最小实现。修复后工具 source SHA 改变，旧候选全部不可导入，必须重采。

固定最小帧数会把 workload 性能混入证据完整性判断；Application 级 wall-clock schema 扩展能提供更强观测，但会扩大到 Function/Application 接线。当前方案直接使用已有真实数据，范围最小且能精确拦截已观察到的漏检。

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

CPU 验证和双路静态复审通过后提交新工具 SHA；随后重新协调 CPU/GPU 独占窗口，采集一组全新三轮候选。只有 8 项 spread 全 PASS 才执行受保护 import 与唯一一次 non-bless COMPARED。

## Risk / rollback

- `0.90` 是证据完整性下限，不是性能阈值。正常同 SHA 样本约 `0.96..0.99`，保留至少 6 个百分点余量。
- 若某 profile 合法使用 frame cap/vsync 或外部等待导致覆盖不足，应先把等待计入 frame time 或把该 profile 标成不可比较；禁止单独绕过守卫。
- 回滚只需撤销工具、测试与文档提交；baseline 在修复验证和新候选批准前保持原字节。
