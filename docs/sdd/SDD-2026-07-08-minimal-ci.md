# SDD-2026-07-08-minimal-ci: 最小 CI（GitHub Actions）（Mini SDD, S1）

## Status
Done（2026-07-09 首次 Actions 绿灯）

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

- 2026-07-09 首次全绿（arch-gate + build-and-test 双 job）。落地过程暴露两个
  「本地永远发现不了」的环境差异，各修一笔：
  1. **runner 无 ATL 组件**：`atlbase.h` C1083。根治：DXC 路径 `CComPtr` 全量换
     `Microsoft::WRL::ComPtr`（Windows SDK 标配），引擎不再依赖 ATL
     （SDD-2026-07-08-dxc-drop-atl-dependency，commit bc4fba1）
  2. **checkout 默认不拉 LFS**：`.lib` 全是指针桩子，链接期 LNK1107。修复：
     build-and-test job 增加 `git lfs pull --include="project/thirdparty/**"`
     （约 1.1GB，排除与构建无关的 RenderControl/）+ `actions/cache` 缓存
     `.git/lfs`，命中时零 LFS 带宽（commit c00f9f1）
- CI 正是靠这两次失败证明了自身价值：两个问题都被本地环境（装了 ATL、LFS 实体齐全）
  长期掩盖
- 后续候选均已落地（2026-07-09）：glslang 死链接清理见
  SDD-2026-07-09-remove-glslang-dead-links；CI 追加 Release 构建 + 双后端软渲染
  冒烟（DX12/WARP 与 Vulkan/lavapipe——mesa 26.1.3 ICD + LunarG loader——均已
  验证转阻断；lavapipe 上车撞出的引擎侧问题见
  SDD-2026-07-09-vulkan-optional-device-caps）+ `paths-ignore`（docs/**、**.md
  纯文档 push 不触发 CI）
