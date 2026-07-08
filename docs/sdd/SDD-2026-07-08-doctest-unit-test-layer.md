# SDD-2026-07-08-doctest-unit-test-layer: doctest 独立单元测试工程

## Status
Done（2026-07-08 实施完成，验证全绿）

## 执行结果（2026-07-08）

- doctest 钉版 **v2.5.3**（Open question 已关闭），SHA-256 记录在 `project/thirdparty/doctest/VERSION.md`；
- 全新生成：删 sln 后 `generate_vs2022.bat` 通过，Tests 工程入列；
- 构建矩阵：`build_tests.bat` / `build_editor.bat` / `build_sandbox.bat` Debug 全部退出码 0；
- `RunTests.bat`：**8 test cases / 89 assertions 全 PASS，退出码 0**；人为改坏断言确认退出码 1（负向验证）；
- 旧入口 `run.bat sandbox vulkan Debug --engine-self-test` PASS 不回归；
- 按名过滤 `--test-case="*StringView*"` 直接调 Tests.exe 生效（经 RunTests.bat 转发时 cmd 引号处理可能吞过滤，已记录到 tools spec）；
- 计划外改动：`hmemory.h` 的 `memory_copy` / `MemoryService` 补 `ASH_API` 导出（Tests.exe 内联展开 `Array::grow` 触发 LNK2019；属预判的 DLL 边界风险，参照 LogService 整类导出先例）；
- 文档同步完成：`docs/VERIFY.md`（fast path + 矩阵两行）、`docs/CODEBASE_MAP.md`（入口/目录/任务表）、`docs/specs/modules/base.md`、`docs/specs/modules/tools.md`。

后续：Task breakdown 第 5 步（EngineSelfTests 按域迁移）为独立 S1 序列，不在本 SDD。

## Context

当前测试安全网：`EngineSelfTests.cpp`（Base/ 下 5466 行单文件，100+ 条 headless 契约测试，
`--engine-self-test` 在 Application 创建前执行，`EntryPoint.h:332`）+ RenderGate / PerfGate 端到端门禁。

存在的问题：

1. `docs/VERIFY.md` 的 fast path 与变更矩阵完全未引用 self-test——安全网存在但验证流程不感知；
2. 单文件巨石难以按域导航与并行编译，且位于 `Base/` 却 include `Function/` `Graphics/` 头，
   违反 `Base ← Graphics ← Function` 分层红线（即将落地的架构边界机械检查会命中它）；
3. 无标准断言 / 按名过滤 / 失败报告，hand-rolled `report_self_test_failure` 模式扩展成本高。

用户决策（2026-07-08）：引入 doctest 建独立 Tests 工程，现有自测经桥接保留、后续分批迁移。

### 新依赖论证（依赖政策要求）

- **为什么现有能力不够**：自研断言无过滤 / 无分测例报告 / 无 CI 友好退出码语义分档；
- **选型**：doctest（MIT，单头文件，成熟维护中）。对比：Catch2 编译开销显著更高；
  googletest 需独立构建工程。doctest 是三者中编译最快、集成面最小的；
- **集成方式**：vendored 单头文件，钉住 release 版本，走 glm/entt 纯头文件先例
  （`project/thirdparty/doctest/`，不新增 premake 三方工程）；
- **传递依赖**：无。**已知漏洞**：无（纯测试期头文件，不进运行时）。

## Goals

- 新增 `Tests` ConsoleApp 工程（doctest runner），链接 Engine.dll，headless、无 GPU；
- 桥接现有 `run_engine_base_self_tests()`，保证旧安全网在新入口下继续全量执行；
- 首批原生 doctest 用例（Base 层：hstring / harray / hserialization），确立写法范式；
- `RunTests.bat` 一键构建+运行，失败返回非零；`VERIFY.md` fast path 与矩阵纳入。

## Non-goals

- 本 SDD 不迁移现有 100+ 条自测（后续按域拆 S1 任务分批迁移，迁完才删
  `Base/EngineSelfTests.*` 与 `--engine-self-test`）；
- 不动 `Sandbox/Tests/SandboxTestRegistry`（运行时冒烟，另一层职责）；
- 不含 CI 接入（独立任务）。

## Current implementation

- Entry points: `EntryPoint.h:332` `--engine-self-test` → `run_engine_base_self_tests()`（已 `ASH_API` 导出，Editor/Sandbox exe 均可触发）
- Modules: `project/src/engine/Base/EngineSelfTests.h/.cpp`
- Data flow: 手写 `test_*() -> bool` 顺序执行，失败经 `report_self_test_failure` 记日志
- Known constraints: Engine 是 DLL（`ASH_API` 导出宏，`Base/hcore.h:12`）；未导出符号无法被外部测试工程直接引用——迁移期用桥接绕过，迁移时逐符号补导出

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| thirdparty | 新增 vendored doctest（单头 + LICENSE + 版本记录） | `project/thirdparty/doctest/doctest.h`、`LICENSE.txt`、`VERSION.md` |
| tests（新） | Tests 工程：premake（以 Sandbox 为模板：ConsoleApp、links Engine、同 includedirs、PostBuild 同步 exe 到 `product/bin64` 使 Engine.dll 相邻） | `project/src/tests/premake5.lua` |
| tests | runner 入口：`DOCTEST_CONFIG_IMPLEMENT` + 自定义 main（复用 EntryPoint 的仓库根定位逻辑设 cwd，再跑 doctest context） | `project/src/tests/TestMain.cpp` |
| tests | 桥接用例：`TEST_CASE` 内调 `run_engine_base_self_tests()`，返回非 0 即 FAIL | `project/src/tests/EngineSelfTestsBridge.cpp` |
| tests | 首批原生用例（新增覆盖，非迁移） | `project/src/tests/Base/hstring_tests.cpp`、`harray_tests.cpp`、`hserialization_tests.cpp` |
| build | 根 workspace 挂 Tests 工程 | `premake5.lua`（`include "project/src/tests"`） |
| scripts | 构建 + 运行入口 | `build_tests.bat`、`RunTests.bat` |
| docs | fast path / 矩阵 / 地图 / spec 同步 | `docs/VERIFY.md`、`docs/CODEBASE_MAP.md`、`docs/specs/modules/base.md`（self-test 条目注明桥接过渡态）、`docs/specs/modules/tools.md` |

### API / contract changes

无引擎运行时 API 变化。新契约：`RunTests.bat` 退出码 0 = 全部通过；
`Tests.exe --test-case=<pattern>` 支持按名过滤（doctest 原生能力）。

### Backend impact

无。Tests 全程不初始化 RHI / 窗口；双后端行为不受影响。

### Performance

引擎运行时零开销。构建新增一个小工程（doctest 头编译开销集中在 Tests 工程内部，
不触碰 Engine/Editor/Sandbox 编译时间）。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 全新生成 | premake 变更（高危路径） | 删 sln 后 `generate_vs2022.bat`，确认 Tests 工程入列 |
| 构建矩阵 | Tests + 既有 target 不回归 | `build_tests.bat Debug`、`build_editor.bat Debug`、`build_sandbox.bat Debug` |
| artifact 同步 | PostBuild 链 | 确认 `product/bin64/Debug-windows-x86_64/Tests.exe` 存在且可运行 |
| 新入口全量 | 桥接 + 首批用例 | `RunTests.bat`：全部 PASS，退出码 0；人为改坏一条断言确认非零退出 |
| 旧入口不回归 | `--engine-self-test` 原路径 | `run.bat sandbox vulkan Debug --engine-self-test` PASS |
| 渲染门禁 | 不需要（无渲染改动），premake 变更不触碰 shader/pass | — |

## Task breakdown

1. vendor doctest + Tests 工程骨架 + 空 main：全新生成 + 构建通过（无行为变化）；
2. 桥接用例 + `RunTests.bat`：旧安全网在新入口全量 PASS；
3. 首批原生 Base 用例：确立断言 / 命名 / 文件组织范式；
4. 文档同步（VERIFY / CODEBASE_MAP / specs）；
5. （后续独立 S1 序列，不在本 SDD）按域迁移 EngineSelfTests → 迁完删旧文件与 CLI 旗标。

## Risks

| Risk | Mitigation |
| --- | --- |
| DLL 未导出符号限制可迁移范围 | 桥接保底；迁移期逐符号补 `ASH_API`，涉及接口面时另立 SDD |
| Tests exe 与 Engine.dll 版本错配（stale dll） | PostBuild 走既有 SyncRuntimeArtifact 链，required 拷贝失败即断构建 |
| 测试依赖 cwd 的文件路径 | 自定义 main 复用仓库根定位逻辑，与引擎行为一致 |
| doctest 头下载/版本漂移 | vendored 钉版本，`VERSION.md` 记录来源 URL 与 SHA |

## Open questions

- doctest 钉哪个 release：取实现时最新稳定版（2.4.x），落地时记录进 `VERSION.md`。
