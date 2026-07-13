# 实施计划索引

本目录保存已批准 SDD 的可执行实施计划。计划用于分解代码、测试、验证和提交边界；长期架构与行为真源仍在 `docs/adr/`、`docs/specs/` 和对应 SDD。

## Active

- `2026-07-13-gpu-performance-observability.md`：大世界 GPU 植被 Phase 0，建立双后端 GPU timing telemetry 与固定 2K Release PerfGate 基线流程。

## Maintenance

- 每个任务必须先写失败测试或可复现检查，再写最小实现。
- 每个任务只提交计划列出的聚焦文件；禁止顺手整理无关代码。
- 计划完成后把稳定结论回写对应 `docs/specs/`，将 SDD 状态改为 Done，并把本计划移到 Archived 小节。
- baseline/golden 仍只能通过各自 bless 流程更新；计划文件不能授权直接编辑受保护基线。
