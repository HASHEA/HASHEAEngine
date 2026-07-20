# Architecture Decision Records

本目录记录跨阶段、长期有效的架构决策。ADR 解释“为什么选择这个边界”，SDD 解释“一次变更如何实现和验证”；两者不能互相替代。

## 约定

- 文件名：`ADR-<YYYY-MM-DD>-<slug>.md`。
- 状态：`Proposed`、`Accepted`、`Superseded`、`Rejected`。
- ADR 一经 `Accepted` 不原地改写历史结论；新决策通过新 ADR supersede 旧记录。
- S3 变更必须在总体 SDD 中链接对应 ADR，并按阶段另写可批准、可验证的 SDD。

## Index

| ADR | Status | Decision |
| --- | --- | --- |
| [ADR-2026-07-13-gpu-driven-instance-runtime](ADR-2026-07-13-gpu-driven-instance-runtime.md) | Accepted | 植被领域与通用 GPU-driven instance runtime 分离，未来静态网格迁入同一底座 |
