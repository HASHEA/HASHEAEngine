# AshEngine Editor 并行协作分工说明

> 本文档用于约束“主线程 + 多个子线程”并行开发编辑器时的分工、边界、验收和测试流程。
>
> 目标：
> - 让每个子线程的职责、写入范围、交付物清楚可见
> - 让每个子模块都有自己的进度文档
> - 明确谁负责安排、谁负责验收、谁负责测试

---

## 1. 角色定义

### 1.1 主线程 / 协调者

主线程不是“另一个开发者”，而是并行协作的统一入口，负责：

1. 分析需求
2. 拆分模块边界
3. 定义任务列表与优先级
4. 分配子线程的写入范围
5. 汇总子线程结果
6. 发现重叠或冲突
7. 安排验收与回归测试
8. 决定最终合并顺序

主线程必须承担：

- 安排
- 收口
- 验收
- 测试结果确认

### 1.2 子线程 / 模块执行者

每个子线程只负责一个清晰的子模块，默认职责是：

1. 只在被分配的目录和文件范围内工作
2. 实现本模块任务
3. 更新本模块进度文档
4. 提交自测结果
5. 明确列出阻塞项和引擎接口缺口

子线程默认 **不负责**：

- 跨模块架构决策
- 和其他子线程抢同一个写入范围
- 最终合并策略
- 最终验收结论

### 1.3 验收人

默认由主线程担任。

如果后续需要，也可以指定“模块验收人”，但当前约定下：

- 主线程负责看 diff
- 主线程负责读进度文档
- 主线程负责决定是否通过验收

### 1.4 测试人

默认仍由主线程担任统一测试收口。

子线程要做本模块最小自测，但最终回归测试归主线程统一执行并记录结果。

---

## 2. 主线程任务下发模板

每次开启一轮并行开发前，主线程必须先给出一张明确的任务卡。任务卡不能只靠聊天里的自然语言临时描述，至少要包含以下字段：

1. 任务编号 / 标题
2. 归属模块
3. 本轮目标
4. 本轮任务清单
5. 优先级
6. 是否允许并行
7. 前置依赖
8. 允许修改文件范围
9. 禁止修改文件范围
10. 预计交付物
11. 子线程自测要求
12. 主线程统一回归测试项
13. 执行人
14. 验收人
15. 预计合并顺序

建议任务卡模板如下：

```md
### 任务卡：EDT-XXX <标题>

- 模块：Workspace / Viewport | Scene / Inspector | Asset / Console / Settings
- 本轮目标：
- 本轮任务清单：
- 优先级：P0 / P1 / P2
- 是否允许并行：是 / 否
- 前置依赖：
- 允许修改：
- 禁止修改：
- 预计交付物：
- 子线程自测要求：
- 主线程统一回归测试项：
- 执行人：
- 验收人：
- 预计合并顺序：
```

没有任务卡时，子线程只能做只读分析、文档整理、阻塞梳理，不能直接扩写实现范围。

---

## 3. 固定模块划分

当前编辑器并行协作固定分成 3 个子模块。

### 3.1 子线程 A：Workspace / Viewport

职责范围：

- `project/src/editor/App/**`
- `project/src/editor/Editor.*`
- `project/src/editor/Panels/ViewportPanel.*`
- `project/src/editor/Services/EditorViewportService.*`

负责内容：

- 工作区布局
- dockspace
- viewport 面板
- 视口状态
- 视口工具栏
- 离屏 RenderTarget 工作流
- 多 viewport 扩展

不负责内容：

- SceneHierarchy
- Inspector 组件编辑
- 资产浏览器
- 控制台

进度文档：

- `docs/EditorProgress.WorkspaceViewport.md`

### 3.2 子线程 B：Scene Shared Services / Inspector

职责范围：

- `project/src/editor/Panels/SceneHierarchyPanel.*`
- `project/src/editor/Panels/InspectorPanel.*`
- `project/src/editor/Services/SceneService.*`
- `project/src/editor/Services/SelectionService.*`
- `project/src/editor/Services/UndoRedoService.*`
- `project/src/editor/Core/EditorSelection.h`

负责内容：

- 实体选择
- Inspector
- 组件编辑
- Undo / Redo 接入
- Scene 编辑工作流
- SceneHierarchy 依赖的共享场景编辑服务

不负责内容：

- viewport 渲染语义
- 资产浏览器
- 控制台

进度文档：

- `docs/EditorProgress.SceneInspector.md`
- `docs/EditorProgress.SceneHierarchy.md`

### 3.3 子线程 C：Asset / Console / Settings

职责范围：

- `project/src/editor/Panels/AssetBrowserPanel.*`
- `project/src/editor/Panels/ConsolePanel.*`
- `project/src/editor/Services/AssetDatabaseService.*`
- `project/src/editor/Services/CommandService.*`
- `project/src/editor/Services/EditorSettingsService.*`

负责内容：

- 资产浏览器
- 控制台
- 命令注册与发现
- editor 设置项
- 面板的小状态持久化

不负责内容：

- 视口输入和布局
- SceneHierarchy
- Inspector 组件编辑

进度文档：

- `docs/EditorProgress.AssetConsole.md`

### 3.4 共享文件归属规则

以下文件或类型属于共享入口，不能由多个子线程无边界同时改动：

- `project/src/editor/App/EditorApplication.*`
  - 默认 owner：子线程 A
- `project/src/editor/Editor.*`
  - 默认 owner：子线程 A
- `project/src/editor/Core/EditorContext.h`
  - 默认 owner：主线程
- `project/src/editor/Core/EditorPanel.*`
  - 默认 owner：主线程
- `project/src/editor/premake5.lua`
  - 默认 owner：主线程

如果子线程发现任务必须修改共享文件：

1. 先在本模块进度文档里记录“越界修改申请”
2. 说明原因、影响范围和临时方案
3. 由主线程重新拆分任务，或显式批准后再修改

没有主线程确认前，子线程不能直接改共享文件。

涉及以下共享语义时，主线程必须先指定唯一 owner，再允许子线程落代码：

- `scene lifecycle reset`
- `shared viewport state`
- `shared commands`

子线程交付时也必须明确说明：

- 是否影响了上述三类共享语义
- 是否需要主线程同步更新其他模块文档或验收项

---

## 4. 每个子模块必须维护的进度文档

每个子模块都必须有一个独立进度文档，不能只在聊天记录里同步。

每份进度文档至少包含这些栏目：

1. 模块范围
2. 长期职责边界
3. 当前负责人
4. 本轮任务
5. 本轮写入范围
6. 责任人矩阵
7. 当前状态
8. 已完成项
9. 验收标准
10. 测试记录
11. 依赖 / 风险 / 阻塞
12. 需要引擎配合的接口缺口
13. 下一步任务
14. 最近一次更新时间

更新规则：

- 子线程开始实现前，先读自己的进度文档
- 子线程完成一轮工作后，必须更新自己的进度文档
- 主线程合并或验收后，也可以回写该文档，修正状态
- 本轮任务结束后，至少要留下：
  - 本轮目标
  - 允许 / 禁止修改范围
  - 自测结果
  - 主线程回归结果
  - 当前阻塞是否已升级给主线程

---

## 5. 主线程必须做的安排

主线程在每轮需求开始时，必须先做以下事情：

1. 明确这次需求属于哪个模块，或跨哪些模块
2. 给出任务列表
3. 标出：
   - 优先级
   - 依赖关系
   - 是否允许并行
   - 每个子线程的写入范围
4. 指定每个子线程需要更新哪份进度文档
5. 指定这一轮的验收方式
6. 指定这一轮至少需要做的测试项
7. 指定共享文件是否允许被触碰
8. 指定最终谁来做统一回归与结论记录

如果两个子线程会写同一批文件：

- 主线程必须先重新拆分任务
- 或者明确顺序执行
- 不能让两个子线程无边界地同时改同一片代码

如果任务跨两个模块：

1. 主线程先拆成两个任务卡
2. 明确谁先做、谁后做
3. 明确交接点和回归测试点

子线程不能因为“顺手方便”而跨模块接管别人的长期职责。

---

## 6. 验收要求

### 6.1 统一验收流程模板

主线程验收时，统一按下面顺序收口：

1. `code review`
2. `premake`
3. `MSBuild`
4. `smoke`

建议主线程在汇总说明或模块进度文档里直接留下：

```md
Acceptance Record
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
```

其中 `MSBuild` 结论必须区分：

- `代码 / 配置失败`
- `环境 / 权限阻塞`

明确示例：

- 若 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒，统一记为“环境 / 权限阻塞”
- 不应直接判为“代码编译失败”
- 应建议提权后重跑

每个子线程交付后，主线程必须做验收。

验收至少包括：

1. 先做代码 review
2. 看改动是否越界
3. 看是否符合当前模块职责
4. 看是否误改了不该碰的目录
5. 看是否更新了对应进度文档
6. 看是否写清验证结果
7. 看是否引入了运行时原生 ImGui 依赖
8. 看是否引入新的引擎接口缺口但没记录

代码 review 的默认口径：

- 先找 bug、行为回退、设计风险、漏测点
- findings 必须先于概述输出
- 如果没有发现问题，也要明确写“未发现明确 findings”，并说明残余风险或未验证项

验收通过的最低标准：

1. 代码 review 没有留下未处理的阻断性 findings
2. 改动没有越界到未授权模块
3. 对应进度文档已更新
4. 本轮自测记录完整
5. 已知风险和缺口已写清
6. 没有把运行时 UI 重新拉回原生 `ImGui::`
7. 主线程确认可进入统一构建 / 回归阶段

如果统一构建阶段的 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：

1. 默认记为“环境 / 权限阻塞”
2. 不应直接判定为“代码编译失败”
3. 主线程应记录阻塞原因，并建议提权后重跑 `MSBuild`

如果以上条件不满足，任务默认视为“未通过验收”，只能回到子线程继续补齐。

只有通过验收，主线程才可以：

- 合并思路
- 继续下一轮串联开发
- 把该任务标记为完成

---

## 7. 测试要求

### 7.1 子线程自测

每个子线程至少要提交本模块最小自测结论：

- 改了哪些功能
- 跑了什么验证
- 没跑什么验证
- 当前已知风险是什么

子线程必须对“本模块功能正确性”负责，不能用“主线程还没统一测”代替自己的最小自测。

### 7.2 主线程统一测试

最终测试由主线程统一负责。

主线程至少要执行：

1. 代码 review
2. `premake5.exe vs2022`
3. `MSBuild Editor Debug x64`
4. 与本轮修改相关的手工冒烟验证

如果第 3 步失败，主线程要区分两类结果：

1. 代码 / 配置失败
2. 环境 / 权限阻塞
   - 例如访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒
   - 这类情况应建议提权重跑，而不是直接把任务记成代码编译失败

如果这一轮涉及多个模块，主线程还要做跨模块回归检查。

例如：

- SceneHierarchy 选择是否还能同步到 Inspector
- 资产浏览器选择是否还能在 Inspector 正确展示
- viewport/dockspace 改动后各面板是否仍能正常打开

主线程负责：

1. 最终构建是否通过
2. 跨模块联动是否回归
3. 哪些已知风险允许带着进入下一轮
4. 统一测试结论回写到哪里

### 7.3 测试结果记录位置

测试结果应至少写到两处之一：

1. 对应模块的进度文档
2. 主线程的汇总说明 / 最终交付说明

建议：

- 子线程写本模块测试
- 主线程写统一回归测试

建议统一使用简短记录模板：

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

## 8. 冲突升级机制

遇到以下情况时，子线程必须暂停扩写范围，并升级给主线程重新安排：

1. 任务需要改共享文件
2. 任务需要跨模块改动
3. 发现引擎接口缺口会阻塞当前实现
4. 两个子线程要写同一文件
5. 当前需求本身跨两个模块但边界尚未拆清

升级时至少记录：

1. 冲突或阻塞标题
2. 触发日期
3. 归属类型：
   - 编辑器实现
   - 引擎接口
   - 构建问题
   - 环境 / 权限问题
   - 设计待定
4. 当前影响
5. 临时绕过方案
6. 需要主线程决定的事项

---

## 9. 当前默认负责人安排

当前默认约定：

- 主线程 / 协调者：负责安排、验收、统一测试
- 子线程 A：Workspace / Viewport
- 子线程 B：Scene / Inspector
- 子线程 C：Asset / Console / Settings

如果后续再增加子线程，必须先补进本文档，再开始长期使用。

---

## 10. 工作流摘要

每轮并行开发按这个顺序执行：

1. 主线程读文档并拆任务
2. 主线程写任务卡并定义每个子线程边界
3. 子线程读取本模块进度文档和本轮任务卡
4. 子线程只在允许范围内实现
5. 子线程更新本模块进度文档并提交自测结果
6. 主线程统一验收
7. 主线程统一跑 `代码 review + premake + MSBuild + 冒烟测试`
8. 主线程汇总结果并回写验收 / 测试结论
9. 必要时再开启下一轮并行任务

---

## 11. 一句话原则

**子线程负责实现和更新本模块进度，主线程负责安排、验收、测试和最终收口。**
