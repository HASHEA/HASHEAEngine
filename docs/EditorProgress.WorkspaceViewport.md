# Editor 模块进度：Workspace / Viewport

## 1. 模块范围

- `project/src/editor/App/**`
- `project/src/editor/Editor.*`
- `project/src/editor/Panels/ViewportPanel.*`
- `project/src/editor/Services/EditorViewportService.*`

## 2. 长期职责边界

负责：

- 工作区布局
- dockspace
- viewport 面板与状态
- viewport 工具栏与交互入口
- 离屏 RenderTarget 工作流
- 多 viewport 扩展

不负责：

- SceneHierarchy 编辑逻辑
- Inspector 组件编辑
- 资产浏览器
- 控制台

## 3. 当前负责人

- 子线程 A
- 主线程负责安排、验收、统一测试

## 4. 本轮任务

- 对应任务卡：`EDT-R1-A Workspace / Viewport 语义增强`
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
- 前置依赖：无
- 预计交付物：
  - 视口语义字段与工具栏基础能力
  - 布局持久化方案或首版接入
  - 更新后的本模块进度文档

## 5. 本轮写入范围

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
- 越界处理：
  - 若必须修改共享入口或其他模块文件，先升级给主线程重新拆任务

## 6. 责任人矩阵

- 执行人：子线程 A
- 主线程负责人：主线程 / 协调者
- 验收人：主线程 / 协调者
- 自测责任人：子线程 A
- 统一回归测试责任人：主线程 / 协调者

## 7. 当前状态

- 状态：进行中
- 当前阶段：从“能显示 RT”向“有编辑语义的工作视图”推进

## 8. 已完成项

- 基于 `UIContext` 的 dockspace 工作区已经恢复
- 默认布局已建立
- `scene` / `game` 两个 viewport 实例已存在
- viewport 已切换到独立离屏 `RenderTarget`
- `ViewportPanel` 已支持：
  - requested size 上报
  - focused / hovered 状态采集
  - 通过 `UIContext` 预览 RT
- `EditorViewportService` 已开始承载：
  - scene / game 视图语义
  - 视口工具状态
  - RT 脏标记与重建请求
- `ViewportPanel` 已增加基础工具栏和状态展示
- `EditorApplication` 已接入 viewport 布局状态持久化文件
- viewport render target 的持有归属已收口到 per-viewport instance / service：
  - `EditorViewportService::ViewportRecord` 持有 `EditorViewportInstance`
  - `EditorViewportInstance.render_target` 是当前 RT 引用落点
  - `ViewportPanel` 只消费实例状态与 RT，不额外持有全局 RT
  - `EditorApplication` 只持有 `EditorViewportService` 和两个 `ViewportPanel`，不再持有单独 viewport RT 成员

## 8.1 Ownership 规则

- RT ownership：
  - 每个 viewport 的 RT 都归属于对应 `EditorViewportInstance`
  - `EditorViewportService::notify_render_target_updated()` 是 service 内部同步 RT / allocated state 的唯一入口
  - `ViewportPanel` 只通过 `resolve_viewport()` 读取对应实例的 RT
- viewport 生命周期：
  - `EditorApplication::initialize()` 中通过 `ensure_viewport("scene") / ensure_viewport("game")` 建立实例
  - `EditorApplication::shutdown()` 中通过 `m_viewportService.clear()` 统一释放实例记录
- 渲染驱动位置：
  - 当前 RT 创建 / 重建仍由 `Editor.cpp` 调用 `EditorViewportService::get_render_request()` 并在重建后回写 service
  - 这说明“ownership 已收口”，但“实际渲染驱动”仍不在 `EditorViewportService` 内部

## 8.2 Primary Viewport 共享状态规则

- `EditorViewportService` 维护唯一 `primary viewport id`
- 共享 `context.viewport` 现在只由 `EditorApplication::update_editor_context()` 每帧基于当前 primary viewport 统一发布
- 非 primary viewport：
  - 保留各自 `EditorViewportInstance.state`
  - 不再由 `ViewportPanel` 轮流覆盖共享 `context.viewport`
- 面板和工具栏判断 primary 身份时，当前统一通过：
  - `EditorViewportService::is_primary_viewport(id)`
  - 不再依赖外部直接比对 viewport 指针

## 8.3 Scene Lifecycle Reset 影响面

- `new scene`
- `startup scene load`
- `reload active scene`

以上路径现在都统一走 `EditorApplication::reset_editor_state_after_scene_change()`：

- 先清空 `SelectionService`
- 再清空 `UndoRedoService`
- 最后重新选中当前场景默认实体

当前口径：

- viewport 的 workspace 级布局、panel open、presentation 状态继续保留
- scene 级选择状态不保留，跟随 active scene 一起重建
- 共享 `context.viewport` 继续只反映当前 primary viewport 快照，不承载 scene reset 广播语义

## 9. 验收标准

主线程验收本模块时至少检查：

1. 改动是否只落在授权范围内
2. 是否没有把 Scene / Inspector 编辑逻辑塞进 viewport 模块
3. 是否继续通过 `UIContext` 呈现运行时 UI
4. 是否补充了本模块自测记录
5. 是否记录了新的引擎接口缺口或阻塞

## 10. 测试记录

- 子线程本轮自测项：
  - 验证 viewport 仍通过 `UIContext` 显示 RT
  - 验证 scene/game 视图状态区分不破坏现有显示
  - 记录未覆盖的相机/拾取缺口
- 子线程本轮自测结果：
  - 已完成静态代码自检
  - scene / game 视图语义、工具栏和 RT 请求链路已接入
- 子线程未覆盖项：
  - 当前尚未接入正式相机、拾取和 gizmo 语义
  - 未由子线程执行 `premake` 与编译，待主线程统一验证
- 主线程统一回归项：
  - dockspace 恢复
  - viewport 显示正常
  - 布局重置 / 恢复不破坏其他面板
- 主线程统一回归结果：
  - 待主线程在实际代码任务中回写
- 主线程 review 修复：
  - 已修复共享 `context.viewport` 被多个 viewport 面板轮流覆盖的问题
  - 现在共享 viewport 状态只由当前 primary viewport 发布
  - Scene / Game 等非 primary 视口仍保留各自实例状态，后续消费者应优先通过 `EditorViewportService` 按 id 查询
  - 已进一步把 primary 判断显式收敛到 `EditorViewportService::is_primary_viewport()`
  - 已把 `context.viewport` 的写入源进一步收口到 `EditorApplication::update_editor_context()`
  - `ViewportPanel` 不再直接写共享 `context.viewport`

## 11. 依赖 / 风险 / 阻塞

- 当前依赖：
  - 依赖 `UIContext` 保持 viewport 呈现原语稳定
  - 依赖 `ScenePresentationSubsystem`、`SceneCameraSource::Override`、`SceneQuery` 继续保持稳定
- 当前风险 / 阻塞：
  - CPU AABB picking 已可用，但复杂模型 / 遮挡下仍需要后续 GPU ID buffer picking
  - DebugDraw 基础形状已可用，但缺 per-viewport / depth / xray 语义，Gizmo 与 selection overlay 仍有一部分留在 Editor 侧 2D overlay
  - 缺少稳定 viewport/render stats facade
  - `EditorViewportInstance` 已定义在 `EditorViewportTypes.h`，后续仍应避免回退依赖整包 `EditorContext`
  - `context.viewport` 仍然是单份 primary viewport 快照；后续如需更多 viewport 消费者，仍应优先按 viewport id 显式走 `EditorViewportService`
  - viewport RT 的创建 / 重建已按 per-viewport instance 组织，但真正的 RT 构建与渲染循环仍需要继续收口
  - 若后续要彻底完成 ownership / 责任收口，主线程需要决定是否把“RT 构建驱动”继续下沉到 `EditorViewportService` 或单独的 viewport renderer 层

## 12. 需要引擎配合的接口缺口

- `AssetType::Mesh` 接入 `instantiate_asset(AssetId)`，方便 viewport 资源投放路径统一
- 统一 scene drop point / ray-to-plane helper，减少 Editor 侧重复拼落点规则
- Scene change event / lifecycle 语义，减少 scene replace/reload 后各 panel 猜状态
- GPU ID buffer picking
- DebugDraw per-viewport / depth / xray 语义
- 视口统计接口

## 13. 下一步任务

- 保持 `ViewportPanel` 主文件只做协调；新增功能优先放 `ViewportPanelToolbar` / `ViewportPanelCanvas` / `ViewportPanelInteraction`
- 继续把 RT 构建驱动从 app/editor 壳层向 viewport service 或独立 renderer 层收口
- 将选中反馈从“强包围盒”继续往更轻量的编辑器表达收敛
- 将 scene / game 的运行与编辑语义进一步拆开
- 继续收敛 viewport 顶部调试信息，减少工作区噪音
- 后续迁移 overlay 时，优先等 Engine 补 per-viewport / depth / xray 语义，不要把临时 2D overlay 继续扩成长期方案

## 14. 最近更新时间

- 2026-05-18
