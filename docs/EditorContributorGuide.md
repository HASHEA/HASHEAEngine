# AshEngine Editor 协作与贡献指南

> 面向参与 `project/src/editor` 开发的人和 AI。
>
> 只保留本地改代码时需要直接执行的范围、验证、交付规则。

## 1. 默认工作范围

- Editor 任务默认只改：
  - `project/src/editor/**`
  - `docs/**`
- 只有在 Editor 任务确实需要新的 Engine 合同接口时，才触碰 `project/src/engine/**`。
- `project/src/engine/Graphics/**` 默认不要碰。
- `KEnginePub/**` 只读参考，不纳入当前实现。
- `product/bin64/**`、`_BUILD/**`、运行期生成配置不是默认源码改动范围。

## 2. 当前运行时 UI 约束

- 活跃运行路径继续走 `UIContext`。
- 不要在活跃路径重新引入：
  - `ImGui::`
  - `imgui.h`
  - `imgui_internal.h`
- 如果缺少 UI 原语，优先补 `UIContext`，不要在 Editor 里重新走原生 ImGui。

## 3. 默认修改原则

- 只改本任务需要的文件。
- 不把无关清理、重命名、目录搬迁混进同一个 diff。
- 默认保持兼容；没有明确授权时，不顺手改用户侧行为。
- 结构整理和行为调整尽量拆开做。
- 发现需要共享入口文件或跨模块改动时，先确认边界再继续写。
- 如果 Editor 任务必须改 `project/src/engine/**`，每一段引擎侧改动都必须用边界注释包起来：
  - `// editor begin 修改原因：...`
  - `// editor end`
- 引擎侧改动只能做“为 Editor 补合同接口 / 补通用能力”的最小修改，不要顺手做无关重构。

## 4. 验证流程

默认顺序：

1. `code review`
2. `premake`
3. `MSBuild`
4. `smoke`

常用命令：

```powershell
.\premake5.exe vs2022
```

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Editor /p:Configuration=Debug /p:Platform=x64
```

运行时冒烟可用：

```powershell
.\run.bat editor dx12 Debug --smoke-test-seconds=5
.\run.bat editor vulkan Debug --smoke-test-seconds=5
```

## 5. 什么改动必须做运行时验证

以下改动不能只停在编译通过：

- startup / shutdown
- dockspace / layout
- scene load / save / reload
- SceneHierarchy / Inspector 联动
- undo / redo
- command / shortcut routing
- viewport 展示、尺寸同步、toolbar
- settings / persistence
- AssetBrowser / Console 基础工作流

最小冒烟检查建议：

1. Editor 能启动
2. 主工作区与 dockspace 正常
3. `Scene Hierarchy`、`Inspector`、`Viewport`、`Asset Browser`、`Console` 能打开
4. SceneHierarchy 选择能同步到 Inspector
5. viewport 有内容，不空白、不崩溃
6. AssetBrowser 搜索 / 过滤仍可用
7. Console 过滤 / 清空仍可用

## 6. `MSBuild` 失败分类

- 如果是代码、配置或工程脚本问题：
  - 记为“代码 / 配置失败”
- 如果因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：
  - 记为“环境 / 权限阻塞”
  - 不要直接记成编译失败
  - 结论里注明“建议提权后重跑”

## 7. 交付时必须写清

- 改了什么
- 有意没改什么
- 跑了哪些验证
- 哪些验证没跑、为什么没跑
- 仍有哪些风险、缺口或兼容性注意事项

建议统一使用：

```md
Acceptance Record
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
```

## 8. 文档更新规则

- 改了架构边界或长期规则，更新 `docs/EditorDeveloperGuide.md`
- 改了代码规范，更新 `docs/EditorCodeStyleGuide.md`
- 改了协作、提交流程或验证要求，更新本文档
- 改了模块状态、风险、下一步，更新对应 `docs/EditorProgress.*.md`
- 改了主入口或阅读顺序，更新 `docs/README.md` 和 `docs/editor/README.md`

## 9. 并行协作时额外遵守

- 先读 `docs/EditorParallelCollaboration.md`
- 先确认本轮允许修改范围
- 完成后更新对应模块进度文档
- 共享入口文件、跨模块改动、Engine 缺口都要显式上报，不要自己默默扩范围
