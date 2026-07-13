# HASHEAEngine

HASHEAEngine（AshEngine）是一个以现代实时渲染和引擎架构实验为目标的 C++17 图形引擎，Vulkan / DX12 双后端，主开发平台 Windows x64，构建系统 Premake5 + MSBuild。仓库包含 Engine DLL、Editor 可执行程序和 Sandbox 验证程序。

项目仍处于持续研发阶段，目标是打通稳定的引擎基础设施与双后端渲染主链路，而不是完整的生产级游戏引擎。

## 能力概览

- Engine 分层：`Base ← Graphics ← Function`，Editor / Sandbox 只依赖 Function 层公共接口
- Vulkan / DX12 双后端 RHI，HLSL 经 DXC 编译（SPIR-V / DXIL），shader 反射驱动绑定布局
- RenderGraph 声明式帧编排 + Deferred 渲染主链路（GBuffer、AO、CSM 阴影、IBL、体积光、Bloom、TAA、tone-map）
- Scene-driven 静态网格渲染：ECS-style Scene → 不可变可见帧 → 渲染线程消费
- 材质 V2：`.AshMat` 基材质 + `.AshMatIns` 实例，engine host shader 与用户材质 shader 拼合
- Editor 基础壳（视口、层级、属性、资产浏览、undo/redo）；Sandbox 为标准验证程序（Sponza 场景）
- 自动门禁：PerfGate（性能回归）、RenderGate（双后端 golden image SSIM 回归 + 跨后端 diff）

骨骼动画、透明队列、asset cooking/streaming、完整 Editor 工具链等仍在演进中。各模块的准确现状见 `docs/specs/`。

## 目录结构

```text
HASHEAEngine/
├── premake5.lua           # 顶层 Premake workspace
├── docs/                  # 文档（入口 docs/README.md）
├── product/               # 运行配置、资产、可执行输出
├── scripts/ tools/        # 构建/验证脚本与本地工具
└── project/
    ├── src/engine/        # Base / Graphics（RHI+双后端）/ Function
    ├── src/editor/        # Editor 可执行项目
    ├── src/sandbox/       # Sandbox 验证程序
    └── thirdparty/        # GLFW、GLM、ImGui、DXC、Tracy 等
```

## 快速开始

环境：Windows x64、Visual Studio 2022（或 MSBuild Build Tools）、支持 Vulkan 或 DX12 的显卡。DXC 与 Vulkan validation layer 运行时已随仓库提供，无需安装 Vulkan SDK。

```bat
generate_vs2022.bat                                :: premake 生成 sln
build_editor.bat Debug / build_sandbox.bat Debug   :: 构建
run.bat editor|sandbox|all [vulkan|dx12] [Debug]   :: 运行（all = 双程序 x 双后端矩阵）
```

构建输出在 `product/bin64/<Config>-windows-x86_64/`。

运行时后端等进程级配置在 `product/config/Engine.ini`（`[RHI] Backend=Vulkan|DX12`，validation 开关仅 Debug 生效）；场景级渲染设置（AO、阴影、Bloom、体积光）在 scene JSON 的 `scene_config` 中。

## 验证

```bat
RunTests.bat                         :: doctest 单元测试
RunArchGate.bat                      :: 分层依赖边界检查
RunRenderGate.bat                    :: 渲染门禁：双后端 golden SSIM 回归 + 跨后端 diff
RunPerfGate.bat -Profile Standard    :: 性能门禁
run.bat sandbox <vulkan|dx12> Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
```

最后一条是 opt-in 的双后端 constant-buffer 可见性诊断；CI 会在 WARP/lavapipe 上把它与 indirect 自测独立执行。按变更类型的完整验证矩阵见 `docs/VERIFY.md`。渲染改动必须双后端验证。

## 文档

| 入口 | 内容 |
| --- | --- |
| [`AGENTS.md`](AGENTS.md) | AI/开发工作流、SDD 风险分级、架构红线、验证要求 |
| [`docs/README.md`](docs/README.md) | 全部文档的路由索引 |
| [`docs/specs/`](docs/specs/README.md) | 模块与 feature 的现状规格（权威出处） |
| [`docs/VERIFY.md`](docs/VERIFY.md) | 按变更类型的验证矩阵 |
| [`docs/CODEBASE_MAP.md`](docs/CODEBASE_MAP.md) | 代码库导航（入口、目录、常见任务） |

本 README 只做仓库入口。模块行为与实现细节以 `docs/specs/` 为准，spec 与代码冲突时以代码为准并修 spec。
