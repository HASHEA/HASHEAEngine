# Editor 模块进度：Asset / Console / Settings

## 1. 模块范围

- `project/src/editor/Panels/AssetBrowserPanel.*`
- `project/src/editor/Panels/ConsolePanel.*`
- `project/src/editor/Services/AssetDatabaseService.*`
- `project/src/editor/Services/CommandService.*`
- `project/src/editor/Services/EditorSettingsService.*`

## 2. 长期职责边界

负责：

- 资产浏览器
- 控制台
- 命令注册与发现
- editor 设置项
- 面板小状态持久化

不负责：

- 视口输入和布局
- SceneHierarchy
- Inspector 组件编辑

## 3. 当前负责人

- 子线程 C
- 主线程负责安排、验收、统一测试

## 4. 本轮任务

- 对应任务卡：`EDT-R1-C Asset / Console / Settings 工作入口升级`
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
- 前置依赖：无
- 预计交付物：
  - AssetBrowser 首版工作流升级
  - Console 结构化展示首版
  - Settings 边界整理结果
  - 更新后的本模块进度文档

## 4.1 第三轮面板体验收口

- 对应任务卡：
  - `EDT-R3-F Asset / Console 面板体验收口`
- 本轮目标：
  - 只用 `UIContext` 收口 AssetBrowser / Console 的最小编辑器体验
  - 为 AssetBrowser 补空态 / 错误态 / 无匹配搜索提示
  - 为 Console 补过滤、级别区分、clear 等最小工作流提示
- 本轮实际写入：
  - `project/src/editor/Panels/AssetBrowserPanel.*`
  - `project/src/editor/Panels/ConsolePanel.*`
  - `docs/EditorProgress.AssetConsole.md`
- 本轮未执行：
  - `premake`
  - 编译
  - 运行时冒烟

## 5. 本轮写入范围

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
- 越界处理：
  - 若必须修改共享入口、日志桥接层或其他模块文件，先升级给主线程重新拆任务

## 6. 责任人矩阵

- 执行人：子线程 C
- 主线程负责人：主线程 / 协调者
- 验收人：主线程 / 协调者
- 自测责任人：子线程 C
- 统一回归测试责任人：主线程 / 协调者

## 7. 当前状态

- 状态：进行中
- 当前阶段：从“只读展示面板”向“真正的资产与日志工作入口”推进

## 8. 已完成项

- AssetBrowser 已支持：
  - 目录侧栏 + 资产内容区两栏工作区
  - breadcrumb 目录导航
  - breadcrumb 拖拽落点
    - 可直接拖到 `All Assets` 或任意父级目录完成 move
  - `Name / Type / State` 列表
  - 文本搜索
  - 按资源类型过滤
  - `Reset Filters`
  - 资产选择同步
  - 资产内容区多选
    - `Ctrl+Click` 切换单项
    - `Shift+Click` 范围选择
    - 右键保持多选并切换主选中项
  - 基础详情区与文本资产预览
  - 资产激活动作入口
  - 资产实例化动作入口
    - toolbar `Instantiate`
    - item context menu `Instantiate`
    - 通过 `InstantiateSceneAssetCommand` 进入统一命令链路
  - 多资源拖拽移动
    - 若当前拖拽命中了已选集合，会携带整组选择一起 move
    - 自动去重父目录 / 子项重叠选择，避免重复移动
  - 批量删除
    - `Delete` 快捷键和删除确认弹窗支持当前多选集合
    - 自动折叠父目录 / 子项重叠选择，避免重复删除
  - 空态 / 错误态 / 无匹配搜索提示
  - 失效目录提示与恢复入口
- Console 已支持：
  - 结构化消息模型
  - 追加消息
  - 文本过滤
  - 按严重级别过滤
  - 顶部严重级别计数按钮快速过滤
  - 按 source 过滤与来源快捷聚焦
  - 清空消息
  - 严重级别计数摘要
  - 日志项右键菜单快速调整过滤/清空
  - 空日志提示
  - 无匹配过滤提示
  - `Reset Filters`
  - `Auto-scroll` 开关
  - `Copy Visible` 复制当前过滤结果到剪贴板
  - 单条日志右键 `Copy Message`
  - `Export Visible` 导出当前过滤结果到 `product/logs/editor`
- AssetDatabaseService 已支持：
  - 设置根目录
  - 刷新
  - 列表读取
  - id 查询
  - load state / error 查询
  - 文本资产读取
- CommandService 已用于菜单动作分发
- CommandService 已支持按前缀发现动作
- EditorSettingsService 已支持 AssetBrowser / Console 视图状态读写

## 9. 验收标准

主线程验收本模块时至少检查：

1. 改动是否只落在授权范围内
2. 是否没有把 Scene / Inspector 或 viewport 逻辑混入本模块
3. 是否明确区分资产浏览器、控制台、设置项的边界
4. 是否补充了日志桥接、设置持久化相关风险和测试记录
5. 是否记录了新的引擎接口缺口或阻塞

## 10. 测试记录

- 子线程本轮自测项：
  - 验证资产搜索/过滤不回退
  - 验证 Console 过滤/清空仍可用
  - 记录资产预览、logger 桥接等未完成缺口
- 子线程本轮自测结果：
  - 已完成代码级静态自查
  - 未执行 `premake` / 编译 / 运行时冒烟，留待主线程统一验证
- 子线程未覆盖项：
  - 当前尚未覆盖资产预览、logger 桥接、跨模块激活联动
  - 当前尚未覆盖空态与过滤提示的运行时视觉验收
- 主线程统一回归项：
  - AssetBrowser 选择与激活动作
  - Console 过滤与清空
  - 设置项变更是否影响工作区恢复
  - AssetBrowser 空态 / 错误态 / 无匹配提示是否符合预期
  - Console 计数、过滤、无匹配提示是否和消息列表一致
- 主线程统一回归结果：
  - `2026-05-29` 已执行 `generate_vs2022.bat`
  - `2026-05-29` 已执行 `build_editor.bat Debug x64`
  - Editor `Debug x64` 编译通过
  - 运行时四路 smoke 本轮未覆盖，留待后续 UI 交互验收

## 11. 依赖 / 风险 / 阻塞

- 当前依赖：
  - 依赖 `AssetDatabaseService`、`CommandService`、`EditorSettingsService` 的边界继续稳定
  - 依赖后续引擎提供 richer 资产预览能力
- 当前风险 / 阻塞：
  - 资产 create / rename / move / delete / reimport 目前仍是 editor-side filesystem fallback，后续最好替换成 engine facade
  - 文本预览之外的缩略图 / mesh / model 预览还没有正式能力
  - 设置项没有版本迁移和更强容错
  - AssetBrowser 的空态 / 错误态仍依赖 `AssetDatabaseService` 当前提供的 `items` 和 `last_error` 粒度
  - 多资源 move / delete 当前已做基础预校验，但仍不是事务性文件系统操作；中途失败时可能留下部分已变更状态
  - Console 已接入 `spdlog -> EditorLogBridge -> EditorEventBus -> ConsolePanel`，但当前仍缺日志复制、按条目复制、导出格式配置等工作流细节

## 12. 需要引擎配合的接口缺口

- 缩略图 / 预览句柄
- GUID / 依赖 / 导入状态查询
- 统一资产 create/move/rename/delete/reimport API
- 资产打开 / 预览 / 实例化入口

## 13. 下一步任务

- 为 `AssetBrowser` 增加批量选择、批量删除、breadcrumb drop、实例化后放置策略
  - 已补首版多选 / 批量删除 / 多资源拖拽移动
- 为 `Console` 增加复制日志、按 source 快捷聚焦持久化和导出格式能力
  - 已补首版复制工作流：
    - toolbar `Copy Visible`
    - item context menu `Copy Message`
- 扩展 `CommandService`
- 收敛 `EditorSettingsService` 配置边界
- 评估 AssetBrowser 是否需要 breadcrumb / 双击进入目录等更强浏览交互
- 评估 Console 是否需要自动滚动、复制日志、按 source 过滤
  - 已补首版 `source` 过滤：
    - 顶部 `Source` 过滤下拉
    - 点击消息来源可快速聚焦该 source
    - 过滤状态持久化到 `EditorSettings`
  - 已补首版 Console 快捷过滤交互：
    - 顶部严重级别计数支持直接切换过滤
    - 日志项右键菜单可快速设置 source / severity 过滤并清空过滤

## 14. 待主线程验收点

- AssetBrowser：
  - 无资产、扫描报错、搜索无结果、目录失效四种状态是否都能给出清晰提示
  - `Reset Filters` / `Reset Directory` / `Clear Search And Filters` 是否符合预期
  - 详情区是否只在有有效选择且当前列表可见时展示
- Console：
  - `Filter`、`Severity`、`Clear`、`Reset Filters` 是否形成顺手的最小工作流
  - `T/I/W/E` 计数是否与消息列表一致
  - 空日志和无匹配过滤两种状态是否易懂

## 15. 最近更新时间

- 2026-05-29
