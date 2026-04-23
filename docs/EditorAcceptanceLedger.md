# AshEngine Editor 统一验收台账

> 用途：
>
> - 作为主线程统一验收的台账入口
> - 汇总当前 round 的模块、验收顺序、结果占位和阻塞记录
> - 不替代各模块自己的进度文档，只做“主线程收口视角”的汇总
>
> 搭配使用：
>
> - 主线程执行步骤与运行时观察项：`docs/EditorFinalIntegrationChecklist.md`

---

## 1. 当前 round 范围

本台账面向当前编辑器统一验收轮次，汇总以下模块：

1. `Workspace / Viewport`
   - 关联任务：
     - `EDT-R1-A Workspace / Viewport 语义增强`
     - `EDT-R2-A Viewport 交互状态收口`
2. `SceneHierarchy`
   - 关联任务：
     - `EDT-R2-B SceneHierarchy 命令化补强`
3. `Inspector`
   - 关联任务：
     - `EDT-R2-C Inspector 扩展机制与属性编辑边界`
4. `Asset / Console / Settings`
   - 关联任务：
     - `EDT-R1-C Asset / Console / Settings 工作入口升级`
5. `UIContext 审计 / 验收支持`
   - 关联任务：
     - `EDT-R2-D UIContext 路径审计与验收支持`

---

## 2. 本轮建议验收顺序

建议主线程按下面顺序收口：

1. `UIContext 审计 / 验收支持`
   - 先确认活跃运行路径没有回退到原生 `ImGui::`
2. `SceneHierarchy`
   - 先看命令边界、reparent、delete restore 等高风险行为
3. `Inspector`
   - 再看命令区与直写区边界是否清楚
4. `Workspace / Viewport`
   - 再确认多视图、RT、布局恢复与 shared state 没有回退
5. `Asset / Console / Settings`
   - 最后看工作入口和设置边界是否稳定
6. `统一构建与冒烟`
   - 固定顺序：
     - `code review`
     - `premake`
     - `MSBuild`
     - `smoke`

---

## 3. 模块验收台账

> 状态建议统一使用：
>
> - `待验收`
> - `验收中`
> - `通过`
> - `阻塞`
> - `回退修改`

| 顺序 | 模块 | 关联任务 | 当前状态 | 主线程结论占位 | 主要阻塞 / 风险占位 |
|---|---|---|---|---|---|
| 1 | UIContext 审计 / 验收支持 | `EDT-R2-D` | 通过 | 运行路径仍走 `UIContext`，未回退原生 `ImGui::` | 后续继续补 UIContext 缺口即可 |
| 2 | SceneHierarchy | `EDT-R2-B` | 通过 | `Create / Reparent / Delete / Rename` 已收口共享命令层，构建与 smoke 通过 | 拖拽 reparent、多选、排序调整仍待后续 |
| 3 | Inspector | `EDT-R2-C` | 通过 | `Name / Transform / Camera / Light / Mesh` 已统一为 `draft + Apply/Revert + shared command` | 仍缺 property 级命令合并与历史展示 |
| 4 | Workspace / Viewport | `EDT-R1-A` + `EDT-R2-A` | 通过 | 多视口离屏 RT 路径正常，`get_viewports()` 顺序已稳定化 | 长远仍建议更多系统直接走 `EditorViewportService` |
| 5 | Asset / Console / Settings | `EDT-R1-C` | 通过 | 构建与 smoke 未见回归，面板正常参与工作区渲染 | 缺更深的交互回归用例与 logger 正式桥接 |

---

## 3.1 跨模块专项验收项

| 专项 | 当前状态 | 主线程结论占位 | 说明 |
|---|---|---|---|
| Scene Lifecycle Reset | 通过 | `EditorApplication` 统一 scene-change helper 已覆盖 `new` / `load` / `reload`，`reload` 失败时真实 fallback 到默认场景 | 检查 `new/load/reload scene` 后 selection / undo-redo / Inspector draft 是否统一重置 |
| Viewport Shared State Semantics | 通过 | `context.viewport` 继续只发布 primary 快照，遍历顺序改为稳定排序 | 检查共享 `context.viewport` 是否只由 primary viewport 经 `EditorApplication` 统一发布 |
| Shared Commands Convergence | 通过 | `Rename / Transform / Camera / Light / Mesh / Create / Reparent / Delete` 已统一进入 `Core/EntityCommands.*` | 检查 `Rename / Transform` 是否已统一到共享命令层，且 Hierarchy / Inspector 语义一致 |

---

## 4. 每模块统一验收记录模板

主线程回写时建议直接复制下面模板：

```md
### <模块名> 验收记录

- Task:
- Owner:
- Review Result:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
- Findings / Risks:
- Next Action:
```

---

## 5. 本轮统一构建与冒烟记录

主线程在模块验收完成后，统一回写：

```md
Acceptance Record
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
```

```md
Smoke Record
- Scope:
- Build:
- Result: pass / fail / blocked
- Covered:
- Not Covered:
- Blockers:
```

---

## 6. 阻塞分类规则

`MSBuild` 阶段必须区分两类结果：

1. `代码 / 配置失败`
2. `环境 / 权限阻塞`

明确口径：

- 如果 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：
  - 统一记为 `环境 / 权限阻塞`
  - 不应直接记为 `代码编译失败`
  - 处理建议统一写为：`建议提权后重跑 MSBuild`

---

## 7. 阻塞记录台账

| 日期 | 模块 | 阻塞类型 | 描述 | 当前处理人 | 状态 |
|---|---|---|---|---|---|
| 2026-04-20 | Editor Debug x64 MSBuild | 环境 / 权限阻塞 | 首次 `MSBuild` 访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒，提权后重跑通过 | 主线程 | 已解决 |

阻塞类型建议统一使用：

- `代码 / 配置失败`
- `环境 / 权限阻塞`
- `引擎接口缺口`
- `UIContext 缺口`
- `设计待定`

---

## 8. 本轮统一结论占位

```md
Round Summary
- Round:
- Modules Accepted:
- Modules Blocked:
- Build Result:
- Smoke Result:
- Engine Gaps Raised:
- Follow-up Owner:
```

### 2026-04-20 验收记录

- Task:
  - 本轮 review findings 收口与遗留风险修复
- Owner:
  - 主线程
- Review Result:
  - 已完成一轮静态 code review；本轮变更未发现新的阻塞级问题
- Premake:
  - `.\premake5.exe vs2022` 通过
- MSBuild:
  - 首次普通权限失败，归类为 `环境 / 权限阻塞`
  - 提权后 `Editor Debug x64` 通过
- Smoke:
  - `product/bin64/Debug-windows-x86_64/Editor.exe --smoke-test=10 --smoke-test-seconds=5` 通过
- Final Result:
  - pass
- Findings / Risks:
  - `UndoRedoService` 仍缺 command coalescing / history UI
  - Inspector 仍是组件级命令，不是 property 级命令
  - SceneHierarchy 仍缺拖拽 reparent / 多选 / 排序调整
- Next Action:
  - 下一轮优先推进 property command、undo 合并、历史面板与层级拖拽
