# SDD-2026-07-09-remove-glslang-dead-links: 移除 glslang 死链接与 vendored 库（Mini SDD, S1）

## Status
Done（2026-07-09 实施完成，验证通过）

## Context

CI LFS 摸底时发现：`project/src/engine/premake5.lua` 链接 glslang 全家桶
（Debug/Release 各 8 个 .lib），但全部 `project/src` 源码**零引用**——没有任何
glslang / SPIRV-Tools 的 API 调用或头文件 include。现行 shader 管线是
HLSL → DXC（dxcompiler.dll）→ SPIR-V，反射走 SPIRV-Cross（源码内编译），
glslang 是早期遗留的死配置。

代价：glslang Debug 库占 LFS 约 1GB（SPIRV-Tools-optd 733MB + SPIRV-Toolsd
146MB + MachineIndependentd 96MB），是 CI `git lfs pull` 1.1GB 的绝对大头。

## Goals

- `premake5.lua`（engine）删除 Debug/Release 两处 glslang includedirs + 16 条 lib 链接
- 删除 `project/thirdparty/glslang/` vendored 目录（死资产，git 历史可找回）
- CI LFS 拉取从 ~1.1GB 降至 ~100MB

## Non-goals

- 不动 DXC / SPIRV-Cross（现行 shader 管线）
- 不清理 LFS 远端历史存储（无收益不值折腾）
- KEnginePub/ 独立遗留 CMake 树不在引擎构建链，不动

## Verification

premake 属 high-risk path（全新构建验证 artifact 同步）：

- 删 `_BUILD/` 后 `generate_vs2022.bat` → `build_editor.bat Debug` + `build_sandbox.bat Debug` 全新构建
- `RunTests.bat`、`RunArchGate.bat`
- `run.bat all Debug --smoke-test-seconds=5`（双后端 smoke，确认运行期无隐性依赖）

## 执行结果

- premake 两处 glslang includedirs + 16 条 lib 链接删除，`project/thirdparty/glslang/`
  整目录 git rm（44 个 LFS 文件中的 22 个，约 1GB）
- 验证：删 `_BUILD/` 全新 generate + build_editor/build_sandbox Debug 0 错误；
  RunTests 16 用例 116 断言全绿 + All Memory Free；ArchGate PASS（35 legacy warns
  不变）；`run.bat all Debug --smoke-test-seconds=5` 四组合全过（sandbox summary
  startup/logic/render 全 passed，clean_exit=yes）
- CI LFS 拉取预期从 ~1.1GB 降至 ~100MB（下轮 CI 缓存 key 变化后生效）
