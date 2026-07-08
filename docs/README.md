# AshEngine 文档入口

> 用途：先决定这次任务该读哪组文档，再进入具体专题。

## 0. 所有任务的公共基线

- `AGENTS.md`（仓库根）
  - AI/协作者规则：命令、架构边界、SDD 分级、变更与验证要求
- `docs/CODEBASE_MAP.md`
  - 仓库导航：入口、目录职责、核心流程、依赖方向、常见任务
- `docs/VERIFY.md`
  - 按变更类型的验证矩阵，改完必须执行
- `docs/CONFIG.md`
  - `product/config/Engine.ini` 配置项权威文档；动配置项前先读、改完同步
- `docs/specs/`
  - 长期现状规格：模块 spec + feature spec；动某模块/feature 前先读对应 spec，索引见 `docs/specs/README.md`
- `docs/sdd/`
  - 变更设计文档（SDD），S1 起需要；模板见 `docs/sdd/TEMPLATE.md`；Done 后结论回写 `docs/specs/`

## 1. 按任务选入口

- Engine 任务：
  - `docs/specs/modules/`（base / graphics / render / render-graph / scene / asset / application）
- Editor 任务：
  - `docs/specs/modules/editor.md`（架构边界与不变式）
  - `docs/editor/EditorCodeStyleGuide.md`（代码规范）
- 工具与门禁任务：
  - `docs/specs/modules/tools.md`
  - `docs/PerfGateUsageGuide.md`、`docs/AIDevDoctor.md`

## 2. 维护规则

- 开始任务前先读入口文档，不要直接在 `docs/` 下盲搜。
- 现状描述以 `docs/specs/` 为准，spec 与代码冲突时以代码为准并修 spec。
- 修改公共行为、边界或验证方式时，优先更新最接近真源的那一份文档，不要在多份文档里重复写同一规则。
- 失效文档直接删除，考古走 git 历史；不保留“历史入口”。
- 保持入口文档短、直接、可执行。
