---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Base

## 职责与边界

`project/src/engine/Base/` 提供与渲染无关的基础设施：日志、内存分配、窗口与输入、基础数据结构、时间与 CPU profiler、二进制序列化、线程模型、文件访问、ini 配置与服务注册。它不依赖 Graphics/Function/Editor 任何上层模块（`window/Window.h` 仅引用 `Graphics/RHIBackend.h` 的 `Backend` 枚举用于窗口配置）；所有上层模块都可以依赖 Base。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `hlog.h/.cpp` | `LogService`（spdlog 封装）+ `HLogTrace/Info/Warning/Error` 宏 |
| `hmemory.h/.cpp` | `MemoryService`、`Allocator` 接口、`Ash_New/Ash_New_Shared/Ash_Delete`、`MemoryStatistics` |
| `window/Window.h`、`window/WindowWin.*` | 窗口抽象 + Windows 实现（内部经 GLFW），`WindowEvent` 事件 |
| `input/Input.h` | `InputState`：键盘（512 键）/鼠标（8 键）/滚轮的帧内状态机 |
| `ds/harray.hpp`、`ds/hhash_map.hpp` | `Array/ArrayView`、`FlatHashMap`（自带 allocator 的容器） |
| `htime.h/.cpp` | tick 计时服务：`time_now` 及 micro/milli/seconds 换算 |
| `hprofiler.h` | Tracy CPU profile 门面宏 `ASH_PROFILE_*`（无 `TRACY_ENABLE` 时全为 no-op） |
| `hserialization.h/.cpp` | 可内存映射 blob 序列化：`Blob/BlobHeader`、`RelativePointer/RelativeArray` |
| `hthreading.h/.cpp` | 线程角色与命令队列：`EngineThreadingConfig`、render/logic/worker 线程 |
| `hfile.h/.cpp` | `file_read_binary/text`、`file_write_binary`、目录操作 |
| `IniConfig.h/.cpp` | `IniConfig` ini 读取器 + `resolve_runtime_config_path/trim_ini_string/to_lower_ascii` |
| `hservice.h`、`hserviceManager.h/.cpp` | `Service` 基类（`ASH_DECLARE_SERVICE`）+ `ServiceManager` 单例注册表 |
| `hstring.*`、`hcache.*`、`hbit.*`、`hcommandQueue.hpp`、`hassert.h` | `StringView/StringBuffer`、`LRUCache`、位操作、命令队列、断言宏 |
| `EngineSelfTests.*` | `run_engine_base_self_tests()`（legacy 自测集，Graphics/Function 域用例），由 `--engine-self-test` 触发，也被 Tests.exe 桥接用例调用；runner 只在服务未初始化时 init/shutdown（谁 init 谁负责，防与 TestMain 双 init）。Base 域用例已迁 doctest（SDD-2026-07-08-selftest-base-migration）；新单测写 doctest（`project/src/tests/`），本文件只减不增 |
| `ProcessMemoryDiagnostics.*` | 进程内存诊断采样 |

## 公共接口

- 日志：`LogService::instance()` 初始化后使用 `HLogInfo/Warning/Error/Trace` 宏；引擎/应用双 logger 由 `ASH_ENGINE` 宏区分。
- 内存：所有引擎堆对象经 `Ash_New<T>(allocator, args...)` / `Ash_Delete` 分配释放；`Allocator` 为抽象接口（`eHeap/eStack/eLinear`）；默认走 `MemoryService::instance()->get_system_allocator()`。
- 窗口：`Window::create()` 工厂 + `init(WindowConfig)`；事件用 `poll_event(WindowEvent&)` 逐个取出，类型见 `WindowEventType`（Resize/Key/Mouse/CloseRequested 等）。
- 输入：`InputState::begin_frame()` / `clear_transient_state()` 清空 pressed/released/scroll，保留 down 与鼠标位置；`merge_frame_snapshot()` 以最新持续状态、逐项 OR 边沿、累加 scroll 的规则合并尚未消费帧。`set_key_state/set_mouse_button_state` 由窗口事件驱动；消费方查询 down/pressed/released。
- 时间：`time_service_init/shutdown` 启停一次，`time_now()` 返回 tick，配套换算函数。
- 线程：`initialize_threading(EngineThreadingConfig)`；`register_current_thread_role` + `is_in_render/logic/worker_thread` 判定；跨线程投递用 `enqueue_render_command` / `pump_render_commands` / `flush_render_commands`；后台任务用 `dispatch_background_task`。
- 配置：`IniConfig::load` + `has_value/get_string/get_bool/try_get_bool`；路径经 `resolve_runtime_config_path` 解析。
- 目录导航：`Directory::path` 的 `k_max_path` 是包含终止符的硬上限，超限输入必须整体失败而不得截断。`file_open_directory` / `file_sub_directory` / `directory_current` 先在局部缓冲区完成解析或打开，再提交路径与句柄；失败保持原状态。替换已打开目录会先释放旧搜索句柄，`file_close_directory` 清空句柄且可重复调用。`..`、嵌套分隔符、正反斜杠等既有路径片段继续交由 Win32 路径规则解析。
- 服务：`Service` 子类声明 `static constexpr const char* k_name` 与 `ASH_DECLARE_SERVICE`；`ServiceManager::get<T>()` 按 `k_name` 哈希惰性注册并返回单例。
- 字符串存储：`StringBuffer` / `StringArray` 必须先 `init` 后使用，`shutdown` 可重复调用；`m_uCurrentSize` 永不超过 `m_uBufferSize`，无法容纳完整写入（含 packed string 的终止符）时保持原状态并失败。`StringArray` 的 map/iterator 与字符区共享一个对齐分配块，shutdown 先析构内部对象再释放；intern 命中 hash 后仍比较字符串内容，碰撞不得把不同字符串别名为同一项。

## 约束与不变式

- Base 不得反向依赖 Graphics/Function/Editor（唯一例外是 `RHIBackend.h` 的纯枚举头）。
- `LogService` 与 `MemoryService` 必须最先初始化（`Application::initialize` 的第一步），其余模块假定二者可用。
- 引擎对象生命周期由 `Ash_New/Ash_Delete` 管理以纳入内存统计；不要混用裸 `new/delete` 分配引擎长生命周期对象。
- `hprofiler.h` 只应在 `.cpp` 中 include，避免 Tracy 头污染公共头。
- Base 符号的 `ASH_API` 导出（如 `memory_copy`、`MemoryService`）仅为跨 DLL 链接需要（Tests.exe / 内联模板在 exe 侧实例化）；**导出不等于开放**，Editor/Game 层禁止直接使用 Base 符号。若测试迁移导致导出面明显膨胀，届时立 SDD 做 Base 静态库拆分。
- 渲染命令队列只能由 render 线程 pump；`InputState` 每线程一份（logic 线程经快照同步，见 application spec）。
- 错误处理宏（`ASH_PROCESS_ERROR` / `ASH_LOG_PROCESS_ERROR` / `ASH_PROCESS_ERROR_EXIT` / `ASH_PROCESS_GUARD_*`）用于资源创建、init/shutdown、绑定/提交/加载等多失败出口的流程函数；纯 getter、轻量转换、一行包装、UI 透传不用。循环体内慎用：失败需终止整个函数时改用显式状态变量或把检查提升到循环外。

## 验证

- `RunTests.bat` 跑 doctest 单测（`project/src/tests/Base/` + legacy 自测桥接），Base 纯逻辑改动必跑。
- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke，Base 被所有目标依赖）。
- `run.bat sandbox vulkan Debug --engine-self-test` 为 legacy 自测入口（Tests.exe 桥接已覆盖同一套用例）。
- 改动波及渲染路径（内存/线程）时按 `docs/VERIFY.md` 追加 `RunRenderGate.bat` 与 PerfGate Standard。

## 历史

- `docs/sdd/SDD-2026-07-08-doctest-unit-test-layer.md`：引入 doctest 单测工程；`hmemory.h` 的 `memory_copy` 与 `MemoryService` 补 `ASH_API` 导出（供 Tests.exe 跨 DLL 链接）。
- `docs/sdd/SDD-2026-07-08-selftest-base-migration.md`：EngineSelfTests Base 域 9 用例迁出 doctest（`tests/Base/hassert|hmemory|hfile_tests.cpp`）；`HeapAllocator/StackAllocator/LinearAllocator` 与 hfile 四函数补 `ASH_API`；`MemoryService` 新增 `is_initialized()`，TestMain 进程级 init/shutdown 服务。
- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：自动化契约测试继续由 Tests.exe 的 legacy bridge 覆盖。
- [SDD-2026-07-12-logic-input-consumption](../../sdd/SDD-2026-07-12-logic-input-consumption.md)：定义跨线程输入快照的批次合并与一次性瞬态消费语义。
- [SDD-2026-07-13-base-string-storage-safety](../../sdd/SDD-2026-07-13-base-string-storage-safety.md)：收紧 `StringBuffer` / `StringArray` 容量边界、allocator/lifetime 配对与 hash collision 正确性。
- [SDD-2026-07-14-directory-path-safety](../../sdd/SDD-2026-07-14-directory-path-safety.md)：目录路径改为有界局部构造和事务式句柄替换，失败不破坏原状态，关闭操作幂等。
