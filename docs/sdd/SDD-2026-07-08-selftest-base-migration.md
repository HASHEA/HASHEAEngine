# SDD-2026-07-08-selftest-base-migration: EngineSelfTests Base 域迁移到 doctest（Mini SDD, S1）

## Status
Done（2026-07-08 实施完成，验证通过）

## Context

SDD-2026-07-08-doctest-unit-test-layer 落地了 doctest 层（Tests.exe + EngineSelfTestsBridge
桥接 legacy 自测集），并约定"legacy 测试按域逐步迁出，EngineSelfTests.cpp 随迁移完成删除"。
本 SDD 是第一个迁移切片：只迁 Base 域 9 个测试（hassert 1、hmemory 5、hfile 2、harray 1），
Graphics/Function 域继续走桥接。

限制条件（调研结论）：
- "导出不等于开放"：EngineSelfTests.cpp 编译在 Engine.dll 内部可访问未导出符号，
  迁到 Tests.exe 后被测符号必须 ASH_API。Base 域增量很小（3 个 allocator 类 + 4 个
  hfile 函数）；Graphics/Function 域会导致导出面大幅膨胀，须另立 SDD（Base 静态库
  拆分或等价方案）后再迁。
- `MemoryService::init` 非幂等：`HeapAllocator::init` 无条件 malloc 新 512MB tlsf 池，
  双重 init 直接泄漏整池。TestMain 进程级 init 一次 + 桥接 runner 里的无条件 init
  会撞上，必须加防重入。
- doctest 不保证 TEST_CASE 注册顺序，新测试不能依赖桥接测试先跑来初始化服务，
  服务初始化必须放 TestMain。

## Goals

- `project/src/tests/Base/` 新增 `hassert_tests.cpp`、`hmemory_tests.cpp`、`hfile_tests.cpp`，
  等价迁移 8 个测试；`test_array_growth_and_initial_size` 已被现有 `harray_tests.cpp`
  覆盖，直接删除不迁
- `TestMain.cpp` 在 `context.run()` 前初始化 LogService + MemoryService（进程一次）
- `MemoryService` / `HeapAllocator` 新增 inline `is_initialized()`；
  `run_engine_base_self_tests` 改为未初始化才 init（`--engine-self-test` 路径语义不变）
- 导出增量：`HeapAllocator` / `StackAllocator` / `LinearAllocator` 类加 ASH_API
  （get_stats / init / get_marker 等非虚方法需要链接符号；虚方法本经 vtable 不需要），
  hfile 的 `file_read_text` / `file_write_binary` / `file_delete` / `file_extension_from_path`
  加 ASH_API
- `EngineSelfTests.cpp` 删除 9 个测试函数与 runner 对应调用行（只减不增）

## Non-goals

- 不迁 Graphics/Function 域测试（导出面膨胀，须另立 SDD）
- 不动 `--engine-self-test` 命令行路径与 EngineSelfTestsBridge
- 不修 `MemoryService::init` 本身的幂等性（服务契约是进程唯一 init，加查询即可）

## Verification

- `generate_vs2022.bat`（premake glob 收新文件）→ `RunTests.bat` 全绿
- `build_editor.bat Debug`（Engine.dll 导出变更）
- `RunArchGate.bat` PASS
- spec 同步：`docs/specs/modules/base.md`（自测集描述）

## 执行结果

- 新增 `tests/Base/hassert_tests.cpp`（1 用例）、`hmemory_tests.cpp`（5 用例）、
  `hfile_tests.cpp`（2 用例）；`test_array_growth_and_initial_size` 已被
  `harray_tests.cpp` 覆盖，直接删除
- 导出增量按计划落地：三个 allocator 类 + hfile 四函数 ASH_API；
  `HeapAllocator::is_initialized()`（查 m_pTlsfHandle）+ `MemoryService::is_initialized()`
- 计划外发现：legacy runner 结尾还**无条件 shutdown** 服务——doctest 用例顺序不保证，
  桥接先跑会拆掉后续用例的堆。改为 `owns_services`（谁 init 谁负责回收）双向守卫；
  TestMain 在 `context.run()` 后对称 shutdown，保留退出时 "All Memory Free" 泄漏诊断
- 验证：`generate_vs2022.bat` → `RunTests.bat` 16 用例 116 断言全绿 + All Memory Free；
  `build_editor.bat Debug` 0 错误；`RunArchGate.bat` PASS（legacy warns 36 → 35）；
  `Editor.exe --engine-self-test` 退出码 0（owns_services 分支）
- spec 同步：`docs/specs/modules/base.md`（EngineSelfTests 条目 + 历史）
