---
owner: huyizhou
last_reviewed: 2026-07-14
review_cycle: monthly
status: active
---

# Codebase Map

给 AI 与新人的仓库导航图：入口、目录、核心流程、公共抽象、依赖方向、常见任务。
描述**当前现状**，不记录历史与计划。结构变化时必须同步更新（触发条件见文末）。

## Entry points

- Application: `project/src/engine/Function/Application.*`（生命周期）；Editor/Sandbox 经 EntryPoint 启动，工作目录被重置到仓库根
- Executable: `product/bin64/<Config>-windows-x86_64/{Editor,Sandbox}.exe`；推荐经 `run.bat` 启动
- Configuration: `product/config/Engine.ini` —— `[RHI] Backend`（Vulkan/DX12）、`[VulkanValidation]`、`[DX12Validation]`、`[RenderDebugView]`；全部配置项见 `docs/CONFIG.md`
- Build: `premake5.lua`（workspace 定义 + PostBuild artifact 同步）→ `generate_vs2022.bat` → `build_editor.bat` / `build_sandbox.bat`
- Default scene: `product/assets/scenes/Sandbox.scene.json`（引用 Sponza，携带相机/灯光/环境/`scene_config`）
- RenderGate: `RunRenderGate.bat` → `scripts/RunRenderGate.ps1`；默认覆盖 sandbox + particles，同一 Sandbox 进程用 `--rhi=<vulkan|dx12>`、`--smoke-test-seconds=<timeout>`、`--dump-frame=<png>`、`--scene=<json>` 完成 readiness smoke + capture；成功条件由 asset epoch/当前帧全 scene packet/非致命 present completion（及动态 capture-ready）驱动，ready arm 会清空 AO/TAA/体积光 history 后在下一同 epoch 帧抓取，golden 在 `tools/render/goldens/<scene>/<backend>.png`
- PerfGate: `RunPerfGate.bat` → `scripts/RunPerfGate.ps1`；`Standard` 跑 Debug Editor/Sandbox × Vulkan/DX12 趋势矩阵，`VegetationFullPipeline` 跑固定 Release Sandbox × Vulkan/DX12、2560×1440、完整无植被场景，并要求双后端 GPU frame/pass-group timing schema v2 与 coverage。
- Unit tests: `RunTests.bat [Config] [doctest args...]` → 构建并运行 `product/bin64/<Config>-windows-x86_64/Tests.exe`（doctest，工程在 `project/src/tests/`，含 legacy `run_engine_base_self_tests()` 桥接）
- ArchGate: `RunArchGate.bat` → `scripts/CheckArchBoundary.ps1`；按 `tools/ai-dev/rules/arch-boundary-rules.json` 扫描 include 判定依赖方向红线，新增越界退出码 1
- CI: `.github/workflows/ci.yml`（GitHub Actions，windows runner）——push/PR 跑 ArchGate、sln 生成、Editor/Sandbox Debug+Release 构建、RunTests，以及 Release 下 DX12/WARP 与 Vulkan/lavapipe readiness smoke（含 indirect 自测）；RenderGate/PerfGate 仍不进 CI

## Directories

| Path | Purpose | Typical changes |
| --- | --- | --- |
| `project/src/engine/Base/` | 平台无关基础设施（日志、内存、窗口、输入、ds） | 少动；新基础能力 |
| `project/src/engine/Graphics/` | RHI 抽象 + Vulkan/DX12 后端 + DXC shader 编译 | RHI 能力扩展、后端 bug 修复 |
| `project/src/engine/Function/Render/` | RenderGraph、SceneRenderer、渲染 Pass、材质、渲染配置 | 渲染 feature 开发主战场 |
| `project/src/engine/Function/Scene/`、`Asset/` | 逻辑场景、资产加载 | 场景/资产能力 |
| `project/src/engine/Shaders/` | Engine HLSL 源码 | 与 Pass 改动配套 |
| `project/src/editor/` | Editor 壳与面板（App/Core/Shell/Panels/Services/Widgets） | Editor 功能开发 |
| `project/src/sandbox/` | 验证程序与内置测试（`Tests/SandboxTestRegistry`） | 新 feature 的验证场景 |
| `project/src/tests/` | doctest 单元测试工程（Tests.exe，链接 Engine.dll） | 新增 Base/纯逻辑单测；`Base/` 子目录按模块分文件 |
| `product/` | 运行配置、资产、日志、可执行输出 | 配置与资产；`bin64/` 为构建产物 |
| `scripts/`、`tools/` | 构建/验证脚本与其规则、基线数据 | 工具链改进 |
| `docs/` | 长期文档；`docs/specs/` 模块与 feature 现状规格；`docs/sdd/` 变更设计文档 | 随代码同步更新 |

不属于当前实现、默认不动：`KEnginePub/`、`RenderControl/`（外部参考库）、`_BUILD/`、`Intermediate/`（生成物）。

## Core flows

### Frame render flow

1. 逻辑 `Scene`（Function/Scene）持有场景状态
2. `ScenePresentationSubsystem` 每帧把 Scene 转换为渲染线程可消费的**不可变可见帧数据**，并附带场景级 `SceneRenderConfig` 快照
3. `SceneRenderer`（Function/Render）消费帧数据，按配置组织 pass：GBuffer → AO → shadow → deferred lighting → Environment/Composite → sky/背景 → GPU particles → 体积光 → Bloom → TAA → tone-map → debug overlay
4. Pass 经 `RenderGraph` 声明资源读写关系，编译出 live pass、transient 生命周期与 barrier plan
5. 执行经 `Renderer`/`RenderDevice` 落到 RHI（`DynamicRHI`），由 Vulkan 或 DX12 后端提交

### Shader flow

1. HLSL 源码位于 `engine/Shaders/`（Editor 另有 `editor/Shaders/`）
2. 经 `Graphics/DXC/` 编译（DXC + 反射），双后端共用绑定约定
3. 材质 V2：`.AshMat` 基材质 + `.AshMatIns` 实例；材质 shader 与 engine shader family 拼合（`Surface.StaticMesh`）

### Backend selection flow

1. 启动读取 `product/config/Engine.ini` `[RHI] Backend`；命令行 `--rhi=<vulkan|dx12>` 优先级更高（RenderGate 用它切后端，不动 ini）
2. `DynamicRHI` 实例化对应后端；Debug/Release 同时编入 Vulkan、DX12、DXC
3. `run.bat <target> <backend>` 通过临时改写 Engine.ini 完成切换并在退出后恢复

## Public abstractions

| Name | Location | Purpose | Constraints |
| --- | --- | --- | --- |
| `DynamicRHI` / `RHIResource` | `engine/Graphics/` | 后端无关的 GPU 接口 | 改动必须双后端等价实现 |
| `IGpuTimingTelemetry` | `engine/Graphics/GpuTimingTelemetryRHI.*` | PerfGate opt-in 的固定容量、延迟 non-blocking GPU timestamp sample | 普通运行默认关闭；exact submit acknowledgement 是 coverage 分母唯一真源；实现必须双后端等价 |
| `RenderGraph` | `engine/Function/Render/` | 帧级声明式 pass/资源编排 | 契约见 `docs/specs/modules/render-graph.md`；不管理 buffer/shader/material 生命周期 |
| `SceneRenderConfig` / `scene_config` | Function/Render + scene json | 场景级渲染开关（AO 模式、阴影、Bloom、体积光） | 随帧快照传递，不可跨帧持有 |
| `ScenePresentationSubsystem` | `engine/Function/` | Scene → 渲染数据的唯一桥 | 见 `docs/specs/modules/scene.md` |
| `DebugDrawService` | `engine/Function/` | frame-local 调试绘制（line/box/circle/cone/axes） | tone-map 后叠加，不参与光照 |
| `UIContext` | `engine/Function/` | Editor 与 Engine 的 UI 交互边界 | Editor 不得绕过它直用 ImGui/Graphics |
| `UINodeEditor` / `UINodeGraphModel` | `engine/Function/Gui/` | 通用节点画布门面与纯数据图模型，封装 `imgui-node-editor` 交互边界 | 第三方库只在 Engine.dll 内使用；Editor 只提交节点/pin/link 数据 |

## Dependency direction

```
Allowed:   Base ← Graphics ← Function ← Editor / Sandbox
Forbidden: Editor/Sandbox → Graphics（或任何 Vulkan/DX12 细节）
           Graphics → Function；Base → 任何上层
           跨模块 import 他人 internal（只依赖公共接口）
```

红线由 `RunArchGate.bat` 机械检查（include 扫描；既有越界在 `tools/ai-dev/rules/arch-boundary-rules.json` 的 legacy 名单里只 WARN 禁增）。

## Common tasks

| Task | Read first | Usually changes | Required tests |
| --- | --- | --- | --- |
| 新增/修改渲染 Pass | 对应 feature spec（`docs/specs/features/`）、`docs/specs/modules/render-graph.md`、相邻 Pass 实现 | `Function/Render/*Pass*`、`engine/Shaders/`、`SceneRenderer` | `RunRenderGate.bat` + PerfGate Standard |
| 修渲染 bug（banding/闪烁等） | 对应 Pass + shader | 同上，diff 尽量小 | 同上；用 RenderDebugView 定位 |
| RHI 能力扩展 | `DynamicRHI.h`、两个后端对应实现 | `Graphics/` 三处（抽象+双后端） | 双后端构建 + `RunRenderGate.bat` + PerfGate Standard + validation 开启；GPU timing/性能归因再跑固定 profile |
| Editor 面板功能 | `docs/specs/modules/editor.md`、`docs/editor/EditorCodeStyleGuide.md` | `editor/Panels/`、`Services/` | Editor smoke run（`run.bat editor`） |
| 场景/资产能力 | `docs/specs/modules/scene.md`、`docs/specs/modules/asset.md` | `Function/Scene/`、`Asset/`、scene json | Sandbox + Editor smoke run |
| 改构建/工具链 | `premake5.lua`、对应脚本 | `scripts/`、`tools/`、根 `*.bat` | `TestAIDevDoctor.ps1` / `TestRunPerfGate.ps1` + 全新构建 |
| Base 层纯逻辑改动 | `docs/specs/modules/base.md`、相邻实现 | `engine/Base/`、`project/src/tests/Base/` | `RunTests.bat` |

## 更新触发

以下任一变化必须同步更新本文：顶层目录增删、入口/启动方式变化、模块边界调整、构建或验证命令变化、新增公共抽象、废弃旧模块。
