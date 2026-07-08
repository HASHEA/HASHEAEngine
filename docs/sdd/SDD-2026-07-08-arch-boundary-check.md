# SDD-2026-07-08-arch-boundary-check: 架构边界机械化检查（Mini SDD, S1）

## Status
Done（2026-07-08 实施完成，验证全绿）

## Context

`AGENTS.md` / `docs/CODEBASE_MAP.md` 的依赖方向红线（`Base ← Graphics ← Function ← Editor/Sandbox`）
目前只靠文档约束，无机械执行。include 约定为模块根相对路径（`#include "Graphics/..."`），
文本扫描即可判定跨层依赖，无需编译器介入。

预扫描已发现 2 处真实越界（见 Legacy 名单），证明缺口存在。

## Goals

- `scripts/CheckArchBoundary.ps1`：扫描 `project/src/` 各层源文件的 `#include "<Layer>/..."`，
  按规则判定禁止边；新增越界 → 退出码 1
- 规则数据外置 `tools/ai-dev/rules/arch-boundary-rules.json`（对齐 AIDevDoctor 规则驱动惯例）
- 既有越界进 `legacy_violations` 名单：只 WARN 不 FAIL，**禁止新增**；名单条目失配（文件已修复/删除）视为 FAIL，强制名单只减不增
- 根入口 `RunArchGate.bat`；自测 `scripts/TestCheckArchBoundary.ps1`

## Non-goals

- 不修复既有越界（EditorIconService / VulkanSwapchain 涉及 Graphics 路径，另立任务）
- 不检查「跨模块 import internal」（无 internal 标记约定，无法机械判定）
- Editor→Base 现为文档允许（reality: 26+ 处 `Base/hlog.h`）；若要按“Editor 不直接用 Base”收紧，
  属政策变更 + 批量重构，另议

## Rules（v1 禁止边）

| From | To (forbidden) |
| --- | --- |
| Base | Graphics, Function |
| Graphics | Function |
| Editor | Graphics |
| Sandbox | Graphics |
| Tests | —（测试工程豁免） |

例外（exceptions，长期合法）：`Base/window/Window.h` → `Graphics/RHIBackend.h`（纯枚举头，base spec 已记录）。

Legacy（legacy_violations，WARN、待修复、禁增）：

| 文件 | 越界 | 处置 |
| --- | --- | --- |
| `engine/Base/EngineSelfTests.cpp` | → Function/Graphics（多处） | doctest 迁移完成后随文件删除 |
| `editor/Services/EditorIconService.cpp` | → Graphics（3 头） | 待修：应走 Function 门面 |
| `engine/Graphics/Vulkan/VulkanSwapchain.cpp` | → `Function/Application.h` | 待修：Graphics 反向依赖 |

## Verification

- `scripts/TestCheckArchBoundary.ps1`：正向（当前仓库 PASS+WARN）、负向（临时注入越界 include → 退出码 1）、
  名单失配（注入 stale 条目 → 退出码 1）
- `RunArchGate.bat` 手跑一次确认输出与退出码
- 文档同步：VERIFY.md（fast path + scripts/tools 行）、CODEBASE_MAP.md、specs/modules/tools.md

## 执行结果

- 当前仓库 `RunArchGate.bat`：**PASS，39 条 legacy WARN**（EngineSelfTests.cpp 37 处 + EditorIconService.cpp 3 处 + VulkanSwapchain.cpp 1 处，与预扫描一致，无未知越界）；例外命中 1 处（Window.h → RHIBackend.h）
- `scripts/TestCheckArchBoundary.ps1` 三项全过：正向 PASS、注入 Editor→Graphics 越界退出码 1、stale 名单条目退出码 1
- 文档同步：AGENTS.md（Commands + Architecture rules）、VERIFY.md（fast path + scripts/tools 行）、CODEBASE_MAP.md（入口 + 依赖方向节）、specs/modules/tools.md（清单/接口/验证/历史）
- 后续修复任务（不在本 SDD）：EditorIconService 走 Function 门面；VulkanSwapchain 去除 Function/Application.h 依赖（Graphics 路径，S2）
