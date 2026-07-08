# SDD-2026-07-08-minimal-ci: 最小 CI（GitHub Actions）（Mini SDD, S1）

## Status
Implementing（2026-07-08）

## Context

仓库有 GitHub 远端（HASHEA/HASHEAEngine）但无任何 CI。所有验证目前只在本地执行，
push 后无机械兜底。三方依赖全部 vendored（含 VulkanSDK/dxc），`premake5.exe` 入库，
构建脚本无交互——`windows-latest` runner（自带 VS2022/MSBuild）可直接复用本地脚本。

## Goals

最小闭环：push/PR 时机械验证「能生成、能编译、单测绿、架构边界干净」。

- Job `arch-gate`：`RunArchGate.bat`（秒级，独立 job 先行失败）
- Job `build-and-test`：`generate_vs2022.bat` → `build_editor.bat Debug` → `build_sandbox.bat Debug` → `RunTests.bat Debug`
- 全部复用本地入口脚本，CI 不复制构建逻辑（单一真源）

## Non-goals

- RenderGate / PerfGate 不进 CI（需 GPU，hosted runner 无）
- 不做构建缓存 / Release 矩阵 / artifact 上传（首版跑通为先，慢了再优化）
- 不加分支保护规则（GitHub 侧配置，用户自行决定）

## Design notes

- runner：`windows-latest`；`shell: cmd` + `call *.bat`（保证退出码回传）
- 触发：push 到 main + pull_request；`concurrency` 取消旧跑
- Tests.exe 全程 headless 无 RHI，hosted runner 可跑；`timeout-minutes` 兜底

## Verification

- 本地等价步骤已全绿（generate / build_editor / build_sandbox / RunTests / RunArchGate）
- workflow YAML 语法本地校验
- **真跑验证需 push 后在 Actions 页确认**——用户 push 时机自定，首次绿灯后本 SDD 才算 Done

## 执行结果

（首次 CI 绿灯后回填）
