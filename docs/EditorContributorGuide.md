# AshEngine Editor 协作与贡献指南

> 这份文档面向参与 `project/src/editor` 的开发者，以及会被主线程分配子任务的协作者。
>
> 目标：
> - 快速说明项目是做什么的
> - 明确入口、关键目录、哪些能改/哪些不要碰
> - 固定常用命令、验证方式、日志与提交要求
> - 减少并行协作时的边界歧义

---

## 1. 项目是什么

`HASHEAEngine` 是一个 C++ 桌面图形引擎项目，当前主线工作是在它之上逐步构建一个可扩展的编辑器，目标是沿着 Unity 风格工作流演进。

当前有效主线只看两部分：

- `project/src/engine`
- `project/src/editor`

`KEnginePub` 仅作为参考资料，不纳入当前实现、评审和改动范围。

---

## 2. 入口与关键目录

### 2.1 入口模块

- 工作区构建入口：
  - `premake5.lua`
- Editor 应用入口：
  - `project/src/editor/Editor.h`
  - `project/src/editor/Editor.cpp`
- Editor 编排入口：
  - `project/src/editor/App/EditorApplication.h`
  - `project/src/editor/App/EditorApplication.cpp`

### 2.2 关键目录

- `project/src/engine/Base`
  - 基础设施：日志、窗口、文件系统、Service、数据结构
- `project/src/engine/Function`
  - Editor 允许依赖的引擎公开接口层
- `project/src/engine/Graphics`
  - RHI / 图形后端实现细节，Editor 不应直接依赖
- `project/src/editor/App`
  - 编辑器生命周期、菜单、工作区编排
- `project/src/editor/Core`
  - Editor 上下文、面板基类、选择/命令基础模型
- `project/src/editor/Services`
  - Selection、Scene、Asset、UndoRedo、Viewport、Settings 等服务
- `project/src/editor/Panels`
  - SceneHierarchy、Inspector、Viewport、AssetBrowser、Console 等面板
- `project/src/editor/Shaders`
  - Editor 专属 HLSL 着色器
- `docs`
  - 协作约定、架构文档、专题文档
- `assets`
  - 项目资源目录
- `product/bin64`
  - 运行输出目录，`Editor.exe` 会被拷到这里

### 2.3 当前运行时 UI 边界

运行时 Editor UI 已经收口到 `UIContext`：

- 可以改：
  - `project/src/editor/App/**`
  - `project/src/editor/Panels/**`
  - `project/src/editor/Services/**`
  - `project/src/editor/Core/**`
- 不应在活跃运行路径重新引入：
  - `ImGui::`
  - `imgui.h`
  - `imgui_internal.h`

以下文件目前是历史参考实现，已被 premake 排除，不属于运行时路径：

- `project/src/editor/ImGui/EditorImGuiLayer.h`
- `project/src/editor/ImGui/EditorImGuiLayer.cpp`
- `project/src/editor/ImGui/EditorStyle.h`
- `project/src/editor/ImGui/EditorStyle.cpp`

---

## 3. 哪些目录能改，哪些不要碰

### 3.1 默认允许改动

如果任务是“编辑器开发”，默认允许改这些目录：

- `project/src/editor/**`
- `docs/**`
- `project/src/editor/premake5.lua`
- 根目录 `premake5.lua`
  - 仅当确实需要调整工作区/构建配置时

### 3.2 默认不要碰

没有明确授权时，不要主动改这些目录：

- `project/src/engine/**`
  - 引擎接口缺口应先记录给引擎同学
- `project/src/engine/Graphics/**`
  - Editor 禁止直接依赖
- `KEnginePub/**`
  - 只读参考，不作为当前实现目标
- `_BUILD/**`
  - 构建中间产物
- `project/src/editor/product/**`
  - 生成输出，不是源码目录
- `product/config/editor/**`
  - 运行期生成配置，除非任务明确要求整理/迁移配置

### 3.3 谨慎处理

这些路径可能出现在 `git status` 中，但要谨慎处理：

- `project/src/engine/product/bin64/**`
  - 这是已跟踪的构建输出，只有在仓库策略明确要求或本次任务确有必要时才一起提交
- `product/bin64/**`
  - 运行输出目录，通常用于本地验证，不默认视为源码改动

---

## 4. 常用命令

以下命令默认在仓库根目录 `D:\个人\WorkSpace\HASHEAEngine` 下执行。

### 4.1 生成工程

```powershell
.\premake5.exe vs2022
```

作用：

- 重新生成 `AshEngine.sln`
- 修改了 Editor 代码或构建配置后，都要至少跑一次

### 4.2 构建 Editor

Debug x64：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Editor /p:Configuration=Debug /p:Platform=x64
```

Release x64：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Editor /p:Configuration=Release /p:Platform=x64
```

说明：

- 本地环境里，MSBuild 可能需要访问用户目录下的 Windows SDK 信息。
- 在受限沙箱里，这条命令常需要提权后才能成功。
- 如果 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：
  - 默认应记为“环境 / 权限阻塞”
  - 不应直接记为“代码编译失败”
  - 建议记录阻塞原因，并在提权后重跑一次 `MSBuild`

### 4.3 构建 Engine

通常 Editor 构建会自动带起 Engine；若只想单独构建 Engine：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Engine /p:Configuration=Debug /p:Platform=x64
```

### 4.4 运行 Editor / Sandbox

推荐统一使用根目录脚本：

```powershell
.\run.bat
```

常见用法：

```powershell
.\run.bat editor dx12 Debug
.\run.bat editor vulkan Debug --smoke-test-seconds=5
.\run.bat sandbox dx12 Debug --smoke-test=10
.\run.bat sandbox vulkan Debug --smoke-test-seconds=5
.\run.bat all Debug --smoke-test-seconds=5
```

说明：

- `run.bat` 会在启动前临时改写 `product/config/Engine.ini` 的 `[RHI] Backend`
- 支持 `Editor` / `Sandbox`
- 支持 `DX12` / `Vulkan`
- 进程退出后会恢复原来的 `Engine.ini`
- `run_editor.bat` 现在只是兼容包装，内部会转调 `run.bat editor ...`

### 4.5 拉代码前的安全流程

如果本地工作树是脏的，建议：

```powershell
git stash push -u -m "before-pull"
& 'D:\Tool\Git\cmd\git.exe' pull --ff-only
git stash pop 'stash@{0}'
```

说明：

- 这是当前仓库里最稳妥的同步方式之一。
- 如果 `stash pop` 有冲突，先解决冲突再继续构建。

### 4.6 测试 / 验证命令

当前仓库没有建立统一的自动化单元测试/集成测试入口。

因此当前“测试”主要指：

1. 先做代码 review
   - 优先找：
     - bug
     - 行为回退
     - 高风险改动
     - 漏测点
   - 结论输出时，先列 findings，再给概述

2. 生成工程

```powershell
.\premake5.exe vs2022
```

3. 编译 Editor

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Editor /p:Configuration=Debug /p:Platform=x64
```

4. 进行手工冒烟验证

当前建议至少覆盖四种运行组合：

```powershell
.\run.bat editor dx12 Debug --smoke-test-seconds=5
.\run.bat editor vulkan Debug --smoke-test-seconds=5
.\run.bat sandbox dx12 Debug --smoke-test-seconds=5
.\run.bat sandbox vulkan Debug --smoke-test-seconds=5
```

如果希望串行跑完整矩阵，也可以直接：

```powershell
.\run.bat all Debug --smoke-test-seconds=5
```

### 4.7 打包 / 发布

当前没有独立的正式打包脚本。

现阶段可用约定：

- `Release` 构建输出可作为交付候选：
  - `product/bin64/Release-windows-x86_64/Editor.exe`
- 顶层工作区存在 `Dist` 配置，但 **Editor 的发布流程尚未标准化**

结论：

- 不要假设已经存在完善的“打包命令”
- 如果后续要做正式打包，应先补一条明确的发布流水线文档或脚本

---

## 5. 当前推荐验证方式

### 5.0 统一验收流程模板

所有编辑器任务在进入“可提交 / 可验收”状态前，统一按下面顺序执行：

1. `code review`
2. `premake`
3. `MSBuild`
4. `smoke`

建议直接按下面模板记录：

```md
Acceptance Record
- Code Review:
- Premake:
- MSBuild:
- Smoke:
- Final Result: pass / fail / blocked
```

`MSBuild` 阶段要额外区分“代码 / 配置失败”和“环境 / 权限阻塞”。

如果 `MSBuild` 因访问 `C:\Users\jiangyuting\AppData\Local\Microsoft SDKs` 被拒而失败：

- 统一归类为“环境 / 权限阻塞”
- 不应直接记为“代码编译失败”
- 结论里应补一句“建议提权后重跑 `MSBuild`”

### 5.1 最小验证

任何 Editor 源码或 premake 改动后，至少做：

1. 先做一轮代码 review

2. 生成工程

```powershell
.\premake5.exe vs2022
```

3. 编译 Editor

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AshEngine.sln /t:Editor /p:Configuration=Debug /p:Platform=x64
```

4. 如涉及运行时 UI，再做手工冒烟验证

### 5.2 运行时冒烟验证

涉及 UI、场景树、视口、资源浏览器、控制台时，建议至少手工检查：

1. Editor 能正常启动
2. 主工作区能显示 dockspace
3. `Scene / Game / Inspector / Asset Browser / Console / Scene Hierarchy` 面板能打开
4. SceneHierarchy 选择能同步到 Inspector
5. Viewport 能显示离屏 RT，而不是空白或崩溃
6. AssetBrowser 搜索/过滤仍可用
7. Console 过滤/清空仍可用

建议记录模板：

```md
Smoke Record
- Scope:
- Build:
- Result: pass / fail / blocked
- Covered:
- Not Covered:
- Blockers:
```

### 5.3 需要特别关注的回归点

- DX12 下采样 back buffer 的崩溃路径
- Viewport RT 重建后空白、遮挡、尺寸抖动
- 运行时路径误引入原生 ImGui
- premake 改动后 `.sln` / `.vcxproj` 不一致

---

## 6. 代码风格与命名约定

当前仓库没有一份独立的强制 style guide，但从主线代码可归纳出这些约定：

### 6.1 命名

- 类型名：
  - `PascalCase`
  - 例如 `EditorApplication`、`SceneHierarchyPanel`
- 函数 / 方法名：
  - 以 `snake_case` 为主
  - 例如 `draw_gui()`、`build_default_dock_layout()`
- 私有成员：
  - `m_` 前缀 + `camelCase`
  - 例如 `m_viewportService`
- 局部变量：
  - `snake_case`
  - 例如 `startup_scene_path`
- 常量：
  - 局部常量通常使用 `k_` 或 `kName` 风格，以现有文件风格为准
- 宏：
  - 全大写下划线
  - 例如 `ASH_EDITOR`

### 6.2 文件组织

- 一个主要类型对应一对 `.h / .cpp`
- 面板放到 `Panels/`
- 服务放到 `Services/`
- 运行时工作区编排逻辑集中在 `App/EditorApplication.*`

### 6.3 Include 约定

- 先包含本模块头文件
- 再包含引擎/项目头
- 最后包含 STL / 第三方头
- Editor 运行时路径不要直接包含原生 ImGui 头

### 6.4 修改原则

- 优先做小步、局部、边界清晰的修改
- 不要顺手重构无关模块
- 不要为了省事把 editor 逻辑塞回 engine

---

## 7. 日志规范

统一使用：

```cpp
HLogTrace(...)
HLogInfo(...)
HLogWarning(...)
HLogError(...)
```

约定：

- Editor 侧日志默认走 `app_logger`
- 日志内容尽量短、可定位、可复现
- 带上关键上下文：
  - 资源名
  - 面板名
  - 尺寸
  - 视口 id
  - 文件路径
- 初始化/前几帧 trace 可以保留，但持续高频日志要谨慎，避免污染 Console

建议：

- 正常状态用 `HLogInfo`
- 风险但不阻断流程用 `HLogWarning`
- 功能失败、资源创建失败、路径错误用 `HLogError`
- 仅调试排障时才加高频 `HLogTrace`

---

## 8. 提交与协作要求

### 8.1 开始前

开始任何 Engine / Editor 开发前，至少先看：

1. `docs/README.md`
2. 如果是 Editor 任务，再看 `docs/editor/README.md`
3. `docs/EditorDeveloperGuide.md`
4. 若任务涉及专题，再看对应专题文档

### 8.2 做完后

如果改了 Editor 功能、协作边界或工作流：

1. 更新对应文档
2. 至少回写：
   - `docs/EditorDeveloperGuide.md`
3. 若新增协作规则或总览信息，也同步更新：
   - `docs/README.md`

### 8.3 提交前最少检查

1. 先做一轮代码 review
2. `premake5.exe vs2022`
3. `MSBuild Editor Debug x64`
4. 如涉及运行时 UI，做一轮手工冒烟验证
5. `git diff` 检查是否混入无关改动

### 8.4 提交内容建议

- 一个提交尽量只解决一个清晰问题
- 不要把无关重构和功能修改混在一起
- 不要提交纯本地产物，除非仓库策略明确要求
- 如果必须带上构建产物，要确认它们确实是仓库当前约定的一部分

### 8.5 拉代码要求

- 本地有改动时，优先 stash 再 pull
- 默认使用快进拉取：

```powershell
& 'D:\Tool\Git\cmd\git.exe' pull --ff-only
```

### 8.6 对子线程 / 并行协作者的要求

- 每个子任务都要限定写入范围
- 不要跨模块顺手改太多文件
- 如果发现需要引擎接口支持，先列缺口，不要直接侵入 `project/src/engine/Graphics`
- 并行协作前，先阅读：
  - `docs/EditorParallelCollaboration.md`
  - 自己对应模块的进度文档
- 主线程下发任务时，必须给出任务卡，至少写清：
  - 本轮目标
  - 允许修改范围
  - 禁止修改范围
  - 自测要求
  - 验收人
  - 统一回归测试项
- 汇报结果时至少写清：
  - 当前职责边界
  - 已完成能力
  - 缺口/风险
  - 下一阶段任务
  - 是否需要引擎配合

### 8.7 并行协作启动顺序

如果本轮要分给多个子线程，统一按下面顺序执行：

1. 主线程先阅读：
   - `docs/README.md`
   - `docs/editor/README.md`
   - `docs/EditorDeveloperGuide.md`
   - `docs/EditorParallelCollaboration.md`
2. 主线程根据当前需求写任务卡，并指定模块边界
3. 每个子线程开工前先读：
   - `docs/EditorParallelCollaboration.md`
   - 自己模块的进度文档
4. 子线程只在被授权的文件范围内开发
5. 子线程完成一轮工作后，先更新自己的进度文档，再提交结果
6. 主线程统一验收
7. 主线程统一运行：
   - 代码 review
   - `.\premake5.exe vs2022`
   - `MSBuild Editor Debug x64`
   - 本轮需要的手工冒烟测试
8. 主线程回写验收与测试结论，再决定是否进入下一轮

### 8.8 子线程测试责任

- 子线程负责本模块最小可用验证
- 主线程负责跨模块回归、最终构建结论和发布前收口
- 子线程不能把未验证功能直接标记为“已完成”
- 如果子线程没有跑某项验证，必须明确写到进度文档里

---

## 9. 当前最重要的限制条件

1. Editor 默认只负责 `project/src/editor`
2. Engine 接口缺口优先记录给引擎同学，不默认直接改 Engine
3. 运行时 UI 必须走 `UIContext`
4. `KEnginePub` 不参与当前实现
5. 当前没有正式自动化测试和打包流水线，验证主要依赖：
   - `code review`
   - `premake`
   - `MSBuild`
   - 手工冒烟测试

---

## 10. 一句话执行摘要

对编辑器协作者来说，最重要的工作流就是：

**先读文档，默认只改 `project/src/editor`，运行时 UI 只走 `UIContext`，改完先做代码 review，再跑 `premake`、`Editor` 编译和需要的冒烟测试，最后补文档。**
