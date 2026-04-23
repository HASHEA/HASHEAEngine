# AshEngine Editor 主线程最终联调执行清单

> 用途：
>
> - 给主线程做最终联调、统一验收时直接勾选使用
> - 基于 `EditorAcceptanceLedger.md` 的模块顺序和验收口径整理
> - 只记录“最终收口动作”和“运行时重点观察项”

---

## 1. 使用前先确认

- [ ] 已阅读 `docs/EditorAcceptanceLedger.md`
- [ ] 已阅读 `docs/EditorToEngineGapChecklist.md`
- [ ] 已收集各模块最新进度文档中的自测结论
- [ ] 已确认本轮需要验收的模块与任务编号

---

## 2. 主线程最终联调步骤

### 2.1 验收前静态收口

- [ ] 先验 `UIContext 审计 / 验收支持`
- [ ] 再验 `SceneHierarchy`
- [ ] 再验 `Inspector`
- [ ] 再验 `Workspace / Viewport`
- [ ] 最后验 `Asset / Console / Settings`
- [ ] 每个模块都已回写验收状态：`待验收 / 验收中 / 通过 / 阻塞 / 回退修改`
- [ ] 每个模块都已确认没有越界改动
- [ ] 每个模块都已确认没有把活跃运行路径拉回原生 `ImGui::`
- [ ] 每个模块都已记录已知风险、未覆盖项和引擎缺口

### 2.2 统一构建流程

- [ ] 执行 `code review`
- [ ] 先记录 findings，再给结论摘要
- [ ] 执行 `premake`
- [ ] 执行 `MSBuild`
- [ ] 若 `MSBuild` 失败，先分类为：
  - [ ] `代码 / 配置失败`
  - [ ] `环境 / 权限阻塞`
- [ ] 若是访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒：
  - [ ] 记为 `环境 / 权限阻塞`
  - [ ] 记录“建议提权后重跑 MSBuild”
- [ ] 构建通过后，再执行运行时 `smoke`

### 2.3 统一冒烟与回写

- [ ] Editor 能启动
- [ ] Dockspace 正常显示
- [ ] `Scene Hierarchy / Inspector / Viewport / Asset Browser / Console` 均可打开
- [ ] 按模块完成运行时观察项检查
- [ ] 将构建结论回写到 `EditorAcceptanceLedger.md`
- [ ] 将 smoke 结论回写到 `EditorAcceptanceLedger.md`
- [ ] 若发现引擎缺口，回写到 `EditorToEngineGapChecklist.md` 或台账阻塞区

---

## 3. 运行时重点观察项

### 3.1 Hierarchy

- [ ] `Add Root` 可用
- [ ] `Add Child` 可用
- [ ] `Rename` 可用
- [ ] `Reparent` 可用
- [ ] `Delete Selected` 可用
- [ ] `New Scene / Reload Scene` 后层级树和当前选择都能同步刷新
- [ ] `Undo / Redo` 下创建、重命名、reparent、删除恢复链路没有明显回退
- [ ] 当前弹窗式 reparent 行为符合本轮预期
- [ ] 删除恢复的“最小子树快照”行为与文档一致
- [ ] Hierarchy 选择变化能同步到 Inspector

重点观察：

- 不允许出现按钮有反应但层级树不刷新
- 不允许出现 reparent 到非法父节点
- 不允许出现删除后 Inspector 悬空引用或异常显示

### 3.2 Inspector

- [ ] 选择实体后 Inspector 能刷新
- [ ] `New Scene / Reload Scene` 后 `draft` 与 `Pending changes` 不残留
- [ ] `Name` 草稿编辑、`Apply Name`、`Revert Name` 可用
- [ ] `Transform` 草稿编辑、`Apply Transform`、`Revert Transform` 可用
- [ ] `Camera / Light / Mesh` 草稿编辑、`Apply`、`Revert` 可用
- [ ] `Hierarchy` 只读区信息显示正常
- [ ] `Asset Inspector` 只读信息显示正常
- [ ] `Undo / Redo` 对 Name / Transform / Camera / Light / Mesh 的命令区行为符合预期
- [ ] `Hierarchy` 与 `Inspector` 的 `Rename` 行为标签和 undo/redo 语义一致
- [ ] `Undo / Redo` 失败时不会误报成功，也不会丢失 redo 历史

重点观察：

- 不允许 Name / Transform 每次键入都无意生成多条 undo
- 不允许 Inspector 切换选择后草稿状态串实体
- 若 Camera / Light / Mesh 修改异常，优先记为“组件级命令回归”或“后续 property 级命令缺口”

### 3.3 Viewport

- [ ] `Scene` / `Game` 视图都能打开
- [ ] Viewport 能显示离屏 `RenderTarget`
- [ ] 窗口尺寸变化后 RT 预览不空白、不明显抖动
- [ ] 工具栏与状态区能正常显示
- [ ] focused / hovered 状态没有明显异常
- [ ] 布局重置 / 恢复后 viewport 仍正常
- [ ] 非 primary viewport 不会错误覆盖共享 viewport 状态
- [ ] 共享 `context.viewport` 与 primary viewport 状态保持一致
- [ ] 多视口渲染 / UI 顺序在多次启动和布局恢复后保持稳定

重点观察：

- 不允许重新采样 swapchain back buffer
- 不允许出现 RT 被 UI 遮挡后无法恢复
- 不允许 scene / game 视图彼此污染状态

### 3.4 Asset

- [ ] 两栏 / 三栏结构显示正常
- [ ] 目录侧栏可用
- [ ] 资产列表 `Name / Type / State` 显示正常
- [ ] 文本搜索可用
- [ ] 类型过滤可用
- [ ] 资产选择同步正常
- [ ] 资产激活动作入口可用
- [ ] 文本资产详情 / 预览显示正常

重点观察：

- 不允许搜索、过滤后选中状态错乱
- 不允许目录切换后详情区仍显示旧资产
- 若缺缩略图、模型预览、实例化入口，记为引擎缺口，不记为当前代码回归

### 3.5 Console

- [ ] 结构化消息列表显示正常
- [ ] 文本过滤可用
- [ ] 严重级别过滤可用
- [ ] 清空消息可用
- [ ] 多条消息追加后界面仍稳定

重点观察：

- 不允许过滤条件变化后消息表格异常错乱
- 不允许清空后仍残留旧消息
- 若缺 engine/app logger 正式桥接，记为引擎缺口，不记为当前代码回归

---

## 4. 问题分类口径

### 4.1 代码回归

满足以下任一情况，优先记为 `代码回归`：

- 已经存在并宣称完成的功能现在不能用
- 本轮代码改动导致旧链路行为退化
- 同一模块已在文档中列为“已完成”，但运行时表现与文档不一致
- 命令边界、状态同步、布局恢复、面板显示出现新错误

典型例子：

- Hierarchy 删除后 Inspector 崩溃或显示错乱
- Inspector 的 `Apply Transform` 不生效
- Viewport RT 变为空白或又回到 back buffer 路径
- Asset 搜索、过滤、选择链路回退
- Console 过滤或清空失效

### 4.2 环境阻塞

满足以下情况，记为 `环境阻塞` 或 `环境 / 权限阻塞`：

- `premake` 或 `MSBuild` 因本机权限、目录访问、SDK 环境问题失败
- 本地依赖、编译工具、权限设置阻断了正常构建
- 与代码本身无直接因果关系，提权或修复环境后可重试

明确示例：

- `MSBuild` 访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒

处理口径：

- 不应直接记为 `代码编译失败`
- 统一写明：`建议提权后重跑`

### 4.3 引擎缺口

满足以下情况，记为 `引擎缺口`：

- Editor 侧逻辑本身已尽量收口，但缺少引擎 Function 层或 `UIContext` 能力，无法自然继续
- 问题不是“现在这段 Editor 代码写错了”，而是“缺正式承载接口”

典型例子：

- Viewport 缺正式 scene render 接口
- Viewport 缺 picking / ID buffer
- Inspector 缺组件元数据 / 反射描述
- Inspector 缺组件 snapshot / apply / restore
- Hierarchy / Inspector / Asset 缺 drag-drop payload
- Console 缺 engine/app logger 正式桥接

处理口径：

- 记录到 `EditorToEngineGapChecklist.md`
- 若当前有临时保守实现，可允许带着缺口进入下一轮

---

## 5. 最终回写模板

```md
Final Integration Summary
- Round:
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Code Regressions:
- Environment Blockers:
- Engine Gaps:
- Final Result: pass / fail / blocked
```
