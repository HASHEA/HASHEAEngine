# SDD 模板

复制本文件为 `docs/sdd/SDD-<YYYY-MM-DD>-<slug>.md`（日期 + 主题，不用递增编号：
协作开发下并行分支各自占号会静默撞号，日期+主题天然无冲突；同日同主题即重复工作信号）。
S1 用 Mini 模板即可；S2/S3 用标准模板，且**批准前不动代码**。
完成后：结论回写对应长期 spec（模块/feature spec），本文件 Status 改为 Done。

---

## Mini SDD（S1）

```markdown
# Mini SDD: <标题>

## Goal

## Non-goals

## Files
<允许修改的文件范围>

## Approach

## Verification
<对照 docs/VERIFY.md 变更矩阵，列出将执行的命令>

## Risk / rollback
```

---

## 标准 SDD（S2 / S3）

```markdown
# SDD-<YYYY-MM-DD>-<slug>: <标题>

## Status
Draft / Review / Approved / Implementing / Done / Superseded

## Context
<为什么要做；关联 bug / 需求>

## Goals
-

## Non-goals
-

## Current implementation
- Entry points:
- Modules:
- Data flow:
- Known constraints:

## Proposal

### Module changes
| Module | Change | Files |
| --- | --- | --- |

### API / contract changes
<RenderGraph API、RHI 接口、材质/shader 绑定约定、scene json schema 等>

### Backend impact
<Vulkan / DX12 双后端各自需要什么；是否有单后端限制>

### Performance
<预期开销；PerfGate 阈值是否需要调整>

## Verification plan
| 验证 | 覆盖 | 命令 |
| --- | --- | --- |

## Task breakdown
<拆成可独立验证的小步；每步给出验收标准>

## Risks
| Risk | Mitigation |
| --- | --- |

## Open questions
-
```
