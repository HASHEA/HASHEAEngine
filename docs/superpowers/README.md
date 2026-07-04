# superpowers 历史归档

本目录是 2026-04 ~ 2026-06 期间的一次性设计/实施文档（plans/ + specs/ 共 34 份），**只作归档，不可当现状依据**。
现状以 `docs/specs/`（长期 spec）与代码为准；变更流程见 `docs/sdd/`。

## 甄别结论（2026-07-04）

| 历史主题 | 现状去向 |
| --- | --- |
| material-system（04-21/04-23/04-27/04-28 共 4 组） | `docs/specs/features/material-system.md` |
| deferred-gbuffer / deferred-lighting（05-12） | `docs/specs/features/deferred-lighting.md` |
| render-graph（05-14） | `docs/specs/modules/render-graph.md` |
| perf-gate（05-18） | `docs/specs/modules/tools.md`、`docs/PerfGateUsageGuide.md` |
| ambient-occlusion（05-20） | `docs/specs/features/ambient-occlusion.md` |
| render-debug-view（05-20） | `docs/specs/features/render-debug-view.md` |
| directional-csm-shadow（05-25）/ sunlight-shadow-pass-split（05-26） | `docs/specs/features/shadows.md` |
| sandbox-scene-config（05-25） | `docs/specs/features/scene-config.md` |
| skybox-ibl（05-25） | `docs/specs/features/skybox-ibl.md` |
| bloom-pass（06-05） | `docs/specs/features/bloom.md` |
| volumetric-lighting（06-05） | `docs/specs/features/volumetric-lighting.md` |
| taa（06-18，仅 plan） | `docs/specs/features/taa.md` |
| agent-core-v0（06-03） | **废弃**：从未实现（代码中无 `engine/Agent/` 层），方向已被 SDD 路线取代 |

本目录不再新增文档；新变更一律走 `docs/sdd/`，结论回写 `docs/specs/`。
