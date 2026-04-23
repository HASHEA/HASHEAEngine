# AshEngine Editor 任务规划

> 本文档用于把“编辑器实现目标”拆成可执行的里程碑、任务批次和子线程任务卡。
>
> 适用范围：
> - `project/src/editor`
> - `docs`
>
> 不直接覆盖：
> - `project/src/engine`
> - `KEnginePub`

---

## 1. 规划目标

当前编辑器规划遵循两条主线：

1. 先把 P0 编辑器骨架补成稳定、可扩展、可回归的工作平台
2. 再把 P1 场景与资源基础能力逐步接进来

本轮规划只面向“编辑器侧可先落地”的任务，不要求引擎先补新接口。

---

## 2. 里程碑拆分

### M0：骨架收口

目标：

- 把当前已有面板、服务、工作区收口成稳定骨架
- 清理模块边界
- 建立统一的任务、验收、测试流程

完成标志：

- 三个模块都有清晰职责边界
- 运行时 UI 继续只走 `UIContext`
- `premake + Editor Debug` 可稳定通过

### M1：P0 能力补齐

目标：

- 把 P0 阶段缺的“可用性”能力补齐
- 让编辑器从“能展示”升级成“能持续编辑”

重点：

- Workspace / Viewport 语义增强
- Scene / Inspector 编辑工作流接入 Undo / Redo
- Asset / Console / Settings 从只读壳升级为基础工作入口

完成标志：

- 布局持久化
- Viewport 工具栏与基础输入控制
- Scene 编辑操作进入命令边界
- Asset / Console 面板具备基础工作流闭环

### M2：P1 最小内容闭环

目标：

- 让编辑器具备最小内容编辑闭环

重点：

- Scene 序列化稳定化
- Inspector 扩展机制
- Asset Database 更完整的浏览/激活能力
- 为后续 gizmo、资源实例化、组件反射打底

完成标志：

- 可创建、保存、打开基础场景
- 可编辑基础组件
- 资产浏览、选择、激活链路稳定

---

## 3. 当前优先级判断

当前最适合进入第一轮并行开发的是 M1 的三条线：

1. `Workspace / Viewport`
   - 把“显示 RT”推进到“有编辑语义的工作视图”
2. `Scene / Inspector`
   - 把“基础编辑”推进到“有命令边界、可接 Undo/Redo”
3. `Asset / Console / Settings`
   - 把“只读面板”推进到“可实际使用的资产与日志入口”

这三条线可以并行，但需要通过主线程控制共享文件和交接点。

---

## 4. 第一轮并行开发策略

### 4.1 可并行部分

- Viewport 语义字段、工具栏、布局持久化
- Scene / Inspector 的命令边界梳理与 Undo/Redo 接入
- AssetBrowser 结构升级、Console 结构化展示、Settings 边界收敛

### 4.2 必须串行或由主线程收口的部分

- `project/src/editor/App/EditorApplication.*`
- `project/src/editor/Editor.*`
- `project/src/editor/Core/EditorContext.h`
- `project/src/editor/Core/EditorPanel.*`
- `project/src/editor/premake5.lua`
- 最终 `代码 review + premake + 编译 + 冒烟测试`

### 4.3 本轮统一验收重点

1. 先完成代码 review，并优先暴露 bug / 回退 / 风险 / 漏测点
2. 运行时 UI 不能回退到原生 `ImGui::`
3. 三个模块不能相互越界写文件
4. 新增引擎接口缺口必须记录，不直接侵入引擎
5. 修改后必须能通过 `premake` 和 `Editor Debug` 构建

---

## 5. 第一轮任务卡

> 对应模块进度文档：
> - Workspace / Viewport：`docs/EditorProgress.WorkspaceViewport.md`
> - Scene / Inspector：`docs/EditorProgress.SceneInspector.md`
> - SceneHierarchy：`docs/EditorProgress.SceneHierarchy.md`
> - Asset / Console / Settings：`docs/EditorProgress.AssetConsole.md`

### 任务卡：EDT-R1-A Workspace / Viewport 语义增强

- 模块：Workspace / Viewport
- 本轮目标：
  - 把 viewport 从“显示离屏 RT”推进到“有编辑语义的工作视图”
- 本轮任务清单：
  - 扩展 `EditorViewportInstance` 的语义字段
  - 明确 `scene` / `game` 视图差异
  - 增加基础 viewport 工具栏
  - 梳理基础输入控制与 hovered/focused 门控
  - 增加布局持久化接线点
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 无强前置依赖
  - 若要改共享入口文件，需主线程批准
- 允许修改：
  - `project/src/editor/App/**`
  - `project/src/editor/Editor.*`
  - `project/src/editor/Panels/ViewportPanel.*`
  - `project/src/editor/Services/EditorViewportService.*`
- 禁止修改：
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Panels/InspectorPanel.*`
  - `project/src/editor/Panels/AssetBrowserPanel.*`
  - `project/src/editor/Panels/ConsolePanel.*`
  - `project/src/editor/Core/EditorContext.h`
  - `project/src/editor/Core/EditorPanel.*`
- 预计交付物：
  - 视口语义字段与工具栏基础能力
  - 布局持久化方案或首版接入
  - 模块进度文档回写
- 子线程自测要求：
  - 验证 viewport 仍通过 `UIContext` 显示 RT
  - 验证 scene/game 视图状态区分不破坏现有显示
  - 记录未覆盖的相机/拾取缺口
- 主线程统一回归测试项：
  - dockspace 恢复
  - viewport 显示正常
  - 布局重置 / 恢复不破坏其他面板
- 执行人：子线程 A
- 验收人：主线程
- 预计合并顺序：第 1 或第 2

### 任务卡：EDT-R1-B Scene / Inspector 命令边界接入

- 模块：Scene / Inspector
- 本轮目标：
  - 把当前基础场景编辑动作纳入清晰命令边界，为 Undo/Redo 做首轮接入
- 本轮任务清单：
  - 为创建/删除/重命名/基础属性编辑梳理命令边界
  - 接入 `UndoRedoService` 的首批核心操作
  - 梳理 `SelectionService` 事件需求
  - 设计 Inspector 扩展机制的最小落点
  - 视情况补充 SceneHierarchy 搜索/过滤的最小框架
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 无强前置依赖
  - 如需改共享基础类型，由主线程拆分
- 允许修改：
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Panels/InspectorPanel.*`
  - `project/src/editor/Services/SceneService.*`
  - `project/src/editor/Services/SelectionService.*`
  - `project/src/editor/Services/UndoRedoService.*`
  - `project/src/editor/Core/EditorSelection.h`
- 禁止修改：
  - `project/src/editor/Panels/ViewportPanel.*`
  - `project/src/editor/Services/EditorViewportService.*`
  - `project/src/editor/Panels/AssetBrowserPanel.*`
  - `project/src/editor/Panels/ConsolePanel.*`
  - `project/src/editor/Core/EditorContext.h`
  - `project/src/editor/Core/EditorPanel.*`
- 预计交付物：
  - 首批进入命令系统的编辑操作
  - Undo/Redo 可接入的调用链
  - 模块进度文档回写
- 子线程自测要求：
  - 验证基础创建/删除/选择链路未退化
  - 验证命令边界不破坏 Inspector 当前编辑
  - 记录仍然直写的编辑操作和原因
- 主线程统一回归测试项：
  - SceneHierarchy 与 Inspector 联动
  - 基础实体创建/删除/选择
  - Undo/Redo 首批操作是否可用或至少不破坏流程
- 执行人：子线程 B
- 验收人：主线程
- 预计合并顺序：第 1 或第 2

### 任务卡：EDT-R1-C Asset / Console / Settings 工作入口升级

- 模块：Asset / Console / Settings
- 本轮目标：
  - 把资产浏览器和控制台从基础展示面板推进到可用工作入口
- 本轮任务清单：
  - 把 AssetBrowser 从平铺列表升级为两栏/三栏结构
  - 增加资产激活动作入口
  - 梳理基础资产操作入口的框架
  - 将 Console 升级为结构化日志视图的首版
  - 收敛 `EditorSettingsService` 的边界与面板状态持久化职责
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 无强前置依赖
  - 若涉及 logger 桥接或跨模块联动，需主线程确认
- 允许修改：
  - `project/src/editor/Panels/AssetBrowserPanel.*`
  - `project/src/editor/Panels/ConsolePanel.*`
  - `project/src/editor/Services/AssetDatabaseService.*`
  - `project/src/editor/Services/CommandService.*`
  - `project/src/editor/Services/EditorSettingsService.*`
- 禁止修改：
  - `project/src/editor/Panels/ViewportPanel.*`
  - `project/src/editor/Services/EditorViewportService.*`
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Panels/InspectorPanel.*`
  - `project/src/editor/Core/EditorContext.h`
  - `project/src/editor/Core/EditorPanel.*`
- 预计交付物：
  - AssetBrowser 首版工作流升级
  - Console 结构化展示首版
  - Settings 边界整理结果
  - 模块进度文档回写
- 子线程自测要求：
  - 验证资产搜索/过滤不回退
  - 验证 Console 过滤/清空仍可用
  - 记录资产预览、logger 桥接等未完成缺口
- 主线程统一回归测试项：
  - AssetBrowser 选择与激活行为
  - Console 过滤与清空
  - 设置项变更是否影响工作区恢复
- 执行人：子线程 C
- 验收人：主线程
- 预计合并顺序：第 2 或第 3

---

## 6. 主线程本轮职责

主线程本轮必须额外承担：

1. 控制共享文件修改权限
2. 审核三个模块是否越界
3. 统一决定合并顺序
4. 统一执行：
   - 代码 review
   - `.\premake5.exe vs2022`
   - `MSBuild Editor Debug x64`
   - 本轮手工冒烟测试
   - 若 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败，记为环境 / 权限阻塞，并提权重跑
5. 把统一回归结果回写到对应进度文档

建议主线程统一使用以下验收模板：

```md
Acceptance Record
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
```

其中 `MSBuild` 的失败结论必须先做分类：

- 若是代码、配置或工程脚本问题，记为“代码 / 配置失败”
- 若是访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒，记为“环境 / 权限阻塞”
- 环境 / 权限阻塞应附带“建议提权后重跑”的说明

---

## 7. 第一轮完成定义

第一轮任务全部完成后，至少应达到：

1. Viewport 模块不再只是 RT 展示壳
2. Scene / Inspector 关键操作进入明确命令边界
3. Asset / Console / Settings 具备首轮工作流升级
4. 三个模块都完成文档、自测、主线程验收和统一构建验证
5. 主线程统一验收包含代码 review 结论
6. 若统一构建失败，主线程已区分“代码 / 配置失败”与“环境 / 权限阻塞”

建议主线程回写统一验收时使用以下简短 smoke 模板：

```md
Smoke Record
- Scope:
- Build:
- Result: pass / fail / blocked
- Covered:
- Not Covered:
- Blockers:
```

如果其中某一项缺少引擎接口支撑：

- 先记录缺口
- 保留 editor 侧扩展点
- 不阻塞其他模块继续推进

---

## 8. 当前执行建议

建议主线程按以下顺序启动第一轮：

1. 先派发 `EDT-R1-B`
   - 因为 Scene / Inspector 的命令边界最容易影响后续工作流定义
2. 同时并行派发 `EDT-R1-A`
   - Viewport 语义增强与 B 线耦合较低，可以同步推进
3. 再并行派发 `EDT-R1-C`
   - Asset / Console / Settings 与前两者耦合最低，适合持续并行
4. 最后由主线程统一收口共享文件、跑 `代码 review + premake + MSBuild + 冒烟`

如果本轮时间只能做一半，优先级建议为：

1. `EDT-R1-B`
2. `EDT-R1-A`
3. `EDT-R1-C`

---

## 9. 第二轮并行开发策略

> 第二轮目标不是再做“大模块粗分”，而是把剩余缺口拆成更细的子任务，避免重叠写文件，也避免子线程空转。

### 9.1 第二轮总体目标

- 收口第一轮留下的职责边界问题
- 补齐 Scene / Inspector 命令化的第二批高价值能力
- 补齐 Asset / Console 的可扩展工作流骨架
- 做一轮运行时 `UIContext` 路径专项审计，为统一验收做准备

### 9.2 第二轮固定约束

- 本轮仍然只做 `project/src/editor` 与 `docs`
- 不主动改 `project/src/engine`
- 运行时路径不允许重新引入原生 `ImGui::`
- 不允许多个子线程同时写同一组源文件
- `代码 review + premake + 统一编译 + 冒烟测试` 由主线程在所有子任务完成后统一执行
- 若 `MSBuild` 因本地 SDK 目录访问被拒，统一记为环境 / 权限阻塞，并建议提权重跑

### 9.3 第二轮任务卡

### 任务卡：EDT-R2-A Viewport 交互状态收口

- 模块：Workspace / Viewport
- 本轮目标：
  - 在不改共享入口文件的前提下，收口 viewport 自身的交互状态与扩展点
- 本轮任务清单：
  - 梳理 `ViewportPanel` / `EditorViewportService` 的职责边界
  - 补齐 scene/game 视图工具状态的最小一致性
  - 补一轮 viewport 面板遮挡、状态展示、尺寸变化相关问题清单
  - 更新模块进度文档中的下一阶段接口缺口
- 优先级：P1
- 是否允许并行：是
- 前置依赖：
  - 不依赖其他第二轮任务完成
- 允许修改：
  - `project/src/editor/Panels/ViewportPanel.*`
  - `project/src/editor/Services/EditorViewportService.*`
  - `docs/EditorProgress.WorkspaceViewport.md`
- 禁止修改：
  - `project/src/editor/App/**`
  - `project/src/editor/Editor.*`
  - `project/src/editor/Core/**`
  - 其他面板与服务
- 预计交付物：
  - Viewport 模块第二轮实现或问题收口
  - 更新后的模块进度文档
- 子线程自测要求：
  - 说明改动覆盖的交互场景
  - 明确未验证项
- 主线程统一回归测试项：
  - scene/game viewport 显示
  - 工具栏与状态展示
  - 尺寸变化后的 RT 行为
- 执行人：Singer
- 验收人：主线程
- 预计合并顺序：第 2

### 任务卡：EDT-R2-B SceneHierarchy 命令化补强

- 模块：SceneHierarchy
- 本轮目标：
  - 把 SceneHierarchy 侧剩余高频编辑动作进一步纳入命令边界
- 本轮任务清单：
  - 梳理并补齐重命名、层级编辑、删除恢复相关命令边界
  - 给 `SelectionService` / `UndoRedoService` 的联动补一轮文档化状态说明
  - 更新模块进度文档中的风险、未覆盖项和下一步计划
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 不依赖其他第二轮任务完成
- 允许修改：
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Services/SceneService.*`
  - `project/src/editor/Services/SelectionService.*`
  - `project/src/editor/Services/UndoRedoService.*`
  - `project/src/editor/Core/EditorSelection.h`
  - `docs/EditorProgress.SceneHierarchy.md`
- 禁止修改：
  - `project/src/editor/Panels/InspectorPanel.*`
  - `project/src/editor/Core/EditorContext.h`
  - `project/src/editor/App/**`
  - 其他模块文件
- 预计交付物：
  - SceneHierarchy 第二轮命令化改进
  - 更新后的模块进度文档
- 子线程自测要求：
  - 说明 create/delete/rename/select 是否受影响
  - 记录仍未命令化的动作
- 主线程统一回归测试项：
  - SceneHierarchy 基础编辑链路
  - Undo/Redo 首批高频动作
- 执行人：Carson
- 验收人：主线程
- 预计合并顺序：第 1

### 任务卡：EDT-R2-C Inspector 扩展机制与属性编辑边界

- 模块：Scene / Inspector
- 本轮目标：
  - 让 Inspector 从硬编码编辑继续向“可扩展的编辑器注册机制”迈一步
- 本轮任务清单：
  - 梳理 `InspectorPanel` 中各组件编辑块的边界
  - 设计并落地最小可用的 Inspector 编辑器注册/分发骨架
  - 明确哪些属性编辑仍保留直写，哪些可进入命令边界
  - 更新模块进度文档中的 Inspector 专项状态
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 与 `EDT-R2-B` 并行，但禁止写同一文件
- 允许修改：
  - `project/src/editor/Panels/InspectorPanel.*`
  - `docs/EditorProgress.SceneInspector.md`
- 禁止修改：
  - `project/src/editor/Panels/SceneHierarchyPanel.*`
  - `project/src/editor/Services/SceneService.*`
  - `project/src/editor/Services/SelectionService.*`
  - `project/src/editor/Services/UndoRedoService.*`
  - `project/src/editor/Core/EditorSelection.h`
  - 其他共享入口文件
- 预计交付物：
  - Inspector 第二轮扩展骨架
  - 更新后的模块进度文档
- 子线程自测要求：
  - 说明 Name / Transform / Camera / Light / Mesh 的编辑路径现状
  - 列出仍依赖后续引擎配合的点
- 主线程统一回归测试项：
  - Inspector 随选择变化刷新
  - 基础属性编辑不破坏现有流程
- 执行人：Dewey
- 验收人：主线程
- 预计合并顺序：第 3

### 任务卡：EDT-R2-D UIContext 路径审计与验收支持

- 模块：UIContext / 运行时路径审计
- 本轮目标：
  - 为本轮统一验收准备一份可执行的 UIContext 审计结果和缺口清单
- 本轮任务清单：
  - 审计 `project/src/editor/App`、`Panels`、`Services` 的运行时活跃路径
  - 标记仍然存在的原生 `ImGui::` / `imgui.h` 依赖位置，并区分：
    - 活跃运行路径
    - 历史参考文件
    - 待迁移但当前未编入运行路径的代码
  - 补充 `UIContext` 缺口清单和建议补口优先级
  - 维护独立进度文档，供主线程后续验收使用
- 优先级：P0
- 是否允许并行：是
- 前置依赖：
  - 无
- 允许修改：
  - `docs/Editor.UIContextGapChecklist.md`
  - `docs/EditorProgress.UIContextAcceptance.md`
  - 如有必要补充只读审计结论到 `docs/README.md`
- 禁止修改：
  - `project/src/editor/**`
  - `project/src/engine/**`
- 预计交付物：
  - 一份清晰的 UIContext 审计结论
  - 一份独立进度文档
  - 给主线程的统一验收检查表
- 子线程自测要求：
  - 审计结果必须写明检索范围与分类标准
- 主线程统一回归测试项：
  - 活跃运行路径不回退到原生 ImGui
  - 文档中的缺口与现状一致
- 执行人：Mill
- 验收人：主线程
- 预计合并顺序：第 4

### 9.4 第二轮建议执行顺序

1. 立即并行启动：
   - `EDT-R2-B`
   - `EDT-R2-C`
   - `EDT-R2-D`
2. 同时启动但控制边界较严：
   - `EDT-R2-A`
3. 四个任务都完成并通过主线程文档验收后，再统一执行：
   - 代码 review
   - `.\premake5.exe vs2022`
   - `MSBuild Editor Debug x64`
   - 手工冒烟测试
   - 若 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒，记为环境 / 权限阻塞，并提权重跑
