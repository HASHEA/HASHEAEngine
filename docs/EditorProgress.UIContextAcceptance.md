# Editor 模块进度：UIContext / 验收支持

## 1. 模块范围

- `docs/Editor.UIContextGapChecklist.md`
- `docs/EditorProgress.UIContextAcceptance.md`
- 必要时补充只读索引说明到 `docs/README.md`

## 2. 长期职责边界

负责：

- 运行时 Editor 活跃路径的 `UIContext` 使用审计
- 原生 `ImGui` 依赖的分类记录
- `UIContext` 缺口清单维护
- 为主线程统一验收提供检查表

不负责：

- 直接修改运行时代码
- 替其他模块实现功能
- 统一构建与运行测试

## 3. 当前负责人

- 子线程 D / Mill
- 主线程负责安排、验收、统一测试

## 4. 本轮任务

- 对应任务卡：`EDT-R2-D UIContext 路径审计与验收支持`
- 本轮目标：
  - 为第二轮统一验收准备一份可执行的 UIContext 审计结果和缺口清单
- 本轮任务清单：
  - 审计 `project/src/editor/App`、`Panels`、`Services` 的运行时活跃路径
  - 分类原生 `ImGui::` / `imgui.h` 依赖位置
  - 补充 `UIContext` 缺口优先级建议
  - 输出主线程可直接使用的验收检查表

## 5. 本轮写入范围

- 允许修改：
  - `docs/Editor.UIContextGapChecklist.md`
  - `docs/EditorProgress.UIContextAcceptance.md`
  - 必要时 `docs/README.md`
- 禁止修改：
  - `project/src/editor/**`
  - `project/src/engine/**`

## 6. 责任人矩阵

- 执行人：Mill
- 主线程负责人：主线程 / 协调者
- 验收人：主线程 / 协调者
- 自测责任人：Mill
- 统一回归测试责任人：主线程 / 协调者

## 7. 当前状态

- 状态：已完成本轮审计
- 当前阶段：等待主线程按检查表执行统一验收

## 8. 已完成项

- 已有基础文档：
  - `docs/Editor.UIContextGapChecklist.md`
- 已知背景：
  - 当前运行时 Editor 活跃路径目标是只通过 `UIContext`
  - 历史 `EditorImGuiLayer.*` 与 `EditorStyle.*` 已不在当前 premake 运行时编译路径
- 本轮已完成：
  - 审计 `project/src/editor/App/**`
  - 审计 `project/src/editor/Panels/**`
  - 审计 `project/src/editor/Services/**`
  - 审计 `project/src/editor/Core/**`
  - 审计 `project/src/editor/Editor.*`
  - 审计 `project/src/editor/ImGui/**`
  - 审计 `project/src/editor/premake5.lua`
  - 输出原生 ImGui 依赖分类结果
  - 补充 `UIContext` 缺口优先级
  - 形成主线程可直接使用的验收清单

## 9. 验收标准

主线程验收本模块时至少检查：

1. 是否清楚区分活跃运行路径与历史参考文件
2. 是否给出了可执行的分类标准
3. 是否补全了 `UIContext` 缺口优先级
4. 是否形成可直接用于统一验收的检查表

## 10. 测试记录

- 子线程本轮自测项：
  - 说明检索范围
  - 说明分类口径
  - 说明哪些结论基于代码路径，哪些基于 premake 排除
- 子线程本轮自测结果：
  - 已按任务卡要求完成只读审计
  - 已区分活跃运行路径、历史参考路径、文档草案路径
  - 已核对 `project/src/editor/premake5.lua` 中对历史 ImGui 文件的排除状态
  - 由于当前沙箱中 `rg` 无法启动，本轮改用 PowerShell `Select-String` 完成检索
- 主线程统一回归项：
  - 运行时路径不回退到原生 ImGui
  - 审计文档与代码现状一致
- 主线程统一回归结果：
  - 待主线程在统一验收阶段回写

## 11. 依赖 / 风险 / 阻塞

- 当前依赖：
  - 依赖当前 `premake` 配置与代码路径保持一致
- 当前风险 / 阻塞：
  - 仓库中存在历史参考文件，容易与活跃路径混淆
  - 若其他子线程引入新的 UI 原语需求，需要同步更新缺口清单
  - 当前验收结果主要基于代码静态审计，运行时冒烟仍需主线程在统一阶段执行

## 12. 需要引擎配合的接口缺口

- 若审计发现确实缺失某类通用 UI 原语，应汇总给引擎同学补到 `UIContext`

## 13. 下一步任务

- 主线程按下面的检查表执行统一验收
- 若其他子线程新增运行时 UI 能力，再同步回写缺口优先级
- 若未来需要 popup / drag-drop / shortcut 输入查询，先回到 `docs/Editor.UIContextGapChecklist.md` 补口再实现

## 14. 主线程统一验收检查表

主线程后续验收时，可直接按下面顺序执行。

### 14.1 代码静态检查

1. 检查活跃运行路径中是否出现原生 ImGui 依赖：
   - `project/src/editor/App/**`
   - `project/src/editor/Panels/**`
   - `project/src/editor/Services/**`
   - `project/src/editor/Core/**`
   - `project/src/editor/Editor.*`
2. 检查关键词：
   - `ImGui::`
   - `imgui.h`
   - `imgui_internal.h`
3. 预期结果：
   - 上述活跃路径中应为 0 命中

### 14.2 历史参考路径隔离检查

1. 检查以下文件是否仍只存在于历史参考路径：
   - `project/src/editor/ImGui/EditorImGuiLayer.*`
   - `project/src/editor/ImGui/EditorStyle.*`
2. 检查 `project/src/editor/premake5.lua` 是否仍通过 `removefiles` 排除了这些文件
3. 预期结果：
   - 历史 ImGui 文件仍未进入运行时构建

### 14.3 UIContext 路径 spot check

重点 spot check 这些活跃入口是否继续通过 `UIContext`：

1. `project/src/editor/App/EditorApplication.cpp`
   - dockspace host
   - dock builder
   - main menu
2. `project/src/editor/Core/EditorPanel.cpp`
   - panel begin/end
3. `project/src/editor/Panels/ViewportPanel.cpp`
   - viewport preview
4. `project/src/editor/Panels/SceneHierarchyPanel.cpp`
   - tree / button
5. `project/src/editor/Panels/InspectorPanel.cpp`
   - input / drag / color / combo / checkbox
6. `project/src/editor/Panels/AssetBrowserPanel.cpp`
   - search / combo / table / preview text
7. `project/src/editor/Panels/ConsolePanel.cpp`
   - filter / severity / table

### 14.4 运行时统一冒烟

在主线程统一构建成功后，建议至少做一轮手工冒烟：

1. Editor 启动
2. Dockspace 正常显示
3. `Scene / Game / Inspector / Asset Browser / Console / Scene Hierarchy` 面板可打开
4. Viewport 能继续显示离屏 RT
5. 菜单、表格、树、输入框、按钮仍正常响应

### 14.5 发现缺口时的处理规则

1. 若发现活跃运行路径重新出现原生 ImGui：
   - 本轮验收直接不通过
2. 若只是遇到新的通用 UI 原语缺口：
   - 先记录到 `docs/Editor.UIContextGapChecklist.md`
   - 再由引擎同学补 `UIContext`
   - 不要直接在 Editor 活跃路径临时引入 `ImGui::`

## 15. 最近更新时间

- 2026-04-16
