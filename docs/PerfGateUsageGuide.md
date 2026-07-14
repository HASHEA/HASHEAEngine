# PerfGate 性能门禁使用说明

PerfGate 是性能敏感改动提交前的标准检查流程。它会自动运行指定目标程序，采集 CPU 帧耗时、按需 GPU frame/pass-group timing、FPS、进程内存、Engine heap、后端内存、draw/pass/dispatch 数量和日志诊断，并生成可追溯报告。

当前标准入口是仓库根目录的 `RunPerfGate.bat`：

```bat
RunPerfGate.bat
```

不带参数运行时会打开控制台交互菜单；带参数运行时会直接转发给 `scripts/RunPerfGate.ps1`。

## 1. 什么时候运行

推荐按下面规则选择：

| 改动类型 | 推荐动作 |
| --- | --- |
| 只改文档、注释、纯脚本展示逻辑 | `RunPerfGate.bat -Profile Standard -SkipBuild -DryRun` |
| 改 Engine、Renderer、RHI、Scene、Asset、Application 生命周期或配置 | `RunPerfGate.bat -Profile Standard` |
| 改性能敏感路径，但刚刚已经完成本地构建 | `RunPerfGate.bat -Profile Standard -SkipBuild` |
| 采集固定 2K Release 无植被完整管线候选 | `RunPerfGate.bat -Profile VegetationFullPipeline` |
| 已确认当前性能状态是新的可接受基线 | `RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline` |

提交前的默认要求是：性能敏感改动至少跑一次完整 `Standard` 门禁。`FAIL` 必须先修；`WARN` 需要判断是否是可接受的趋势变化。

## 2. 常用命令

打开交互菜单：

```bat
RunPerfGate.bat
```

完整标准门禁，包含构建：

```bat
RunPerfGate.bat -Profile Standard
```

跳过构建，直接运行已存在的可执行文件：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild
```

在保持 profile 的场景、分辨率、相机和其他运行时约束不变时，关闭 GPU timing 做采集开销 A/B：

```bat
RunPerfGate.bat -Profile VegetationFullPipeline -TelemetryMode Off -SkipBuild
```

该模式仍要求固定运行时 profile 产出完整 schema v2 runtime 元数据，但不会要求已关闭的 GPU timing `backend_info`、adapter 或 driver。结果标记为 GPU baseline 不可比，且不能与 `-BlessBaseline` 同时使用。

只验证脚本流程和报告生成，不启动目标程序：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -DryRun
```

把当前通过门禁的结果写成新基线：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
```

把已由用户批准、SHA-256 锁定的固定 profile 候选报告导入基线（不重新运行目标程序）：

```bat
RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release -BlessBaselineFromReport Intermediate\test-reports\perf-gate\<run>\summary.json -ExpectedReportSha256 <64-hex-sha256>
```

指定基线文件：

```bat
RunPerfGate.bat -Profile Standard -BaselinePath tools\perf\perf_gate_baselines.json
```

## 3. Standard 门禁会做什么

`Standard` profile 定义在：

```text
tools/perf/perf_gate_baselines.json
```

当前 `Standard` 配置为：

| 项目 | 当前值 |
| --- | --- |
| Configuration | `Debug` |
| Warmup | `10` 秒 |
| Sample | `30` 秒 |
| Timeout | `90` 秒 |
| Targets | `Sandbox`、`Editor` |
| Backends | `Vulkan`、`DX12` |

也就是一次完整运行会覆盖：

| Target | Backend |
| --- | --- |
| Sandbox | Vulkan |
| Sandbox | DX12 |
| Editor | Vulkan |
| Editor | DX12 |

脚本会按矩阵切换 backend，运行对应程序，采集 telemetry，并扫描 stdout/stderr 中的 validation、debug-layer、leak 和异常模式。

### VegetationFullPipeline 固定候选

`VegetationFullPipeline` 定义在 `tools/perf/perf_gate_profiles.json`，用于大世界植被 Phase 0 及后续性能归因。它不是 Standard 的替代品，而是固定的 Release benchmark：

| 项目 | 固定值 |
| --- | --- |
| Configuration / target | `Release` / `Sandbox` |
| Backends | `Vulkan`、`DX12` |
| Extent | `2560×1440` actual client extent；不接受静默缩小 |
| Scene | `product/assets/scenes/VegetationBaseline.scene.json`（无植被、完整 pipeline） |
| Warmup / sample / drain / timeout | `10 / 30 / 5 / 90` 秒 |
| Runtime | fixed camera、vsync off、frame cap off、performance validation off |
| GPU timing | required；总 coverage 与 11 个 required metric coverage 均至少 95% |

该 profile 的已批准回归阈值取“相对比例”和“绝对噪声 floor”中的较大值：

| 指标 | WARN 阈值 |
| --- | --- |
| CPU avg / p95 / p99 | `max(5%, 0.25 ms)` / `max(8%, 0.50 ms)` / `max(12%, 1.00 ms)` |
| Private bytes / Engine heap | `max(5%, 128 MiB)` / `max(10%, 1 MiB)` |
| Draw calls | `0`（任何上升都 WARN） |
| `GPU.Frame` avg / p95 | `max(5%, 0.25 ms)` / `max(8%, 0.50 ms)` |
| pass baseline avg `>= 0.5 ms` | avg `max(8%, 0.10 ms)`；p95 `max(12%, 0.20 ms)` |
| pass baseline avg `[0.1, 0.5) ms` | avg `max(15%, 0.05 ms)`；p95 `max(20%, 0.10 ms)` |
| pass baseline avg `< 0.1 ms` | avg `+0.03 ms`；p95 `+0.05 ms`（absolute-only） |

`GPU.Frame` 只使用专用阈值，不进入 pass tier。其余十个 required metric 由 baseline avg 选择 tier，avg/p95 各自独立产生 WARN。三轮候选的稳定性按 target+backend 独立汇总，不混合 Vulkan/DX12：CPU avg 与 `GPU.Frame` avg 的 max-min spread 不得超过 `max(3%, 0.15 ms)`，对应 p95 不得超过 `max(5%, 0.30 ms)`。

正式候选命令：

```bat
RunPerfGate.bat -Profile VegetationFullPipeline
```

正确性验证与正式性能 run 分离：Vulkan validation、DX12 debug/GPU validation 应用短 Debug run 验证 query/reset/resolve/fence 生命周期；正式 Release 数字必须保持 validation off，并由 telemetry 回报实际状态。

## 4. 输出文件在哪里

每次运行会生成一个独立目录：

```text
Intermediate/test-reports/perf-gate/<timestamp>/
```

常用文件：

| 文件 | 含义 |
| --- | --- |
| `summary.md` | 人读的汇总报告，提交前主要看它 |
| `summary.json` | 机器可读汇总，适合后续 CI 或脚本处理 |
| `Sandbox-Vulkan.json` 等 | 单个目标和后端的 telemetry 原始结果 |
| stdout/stderr 日志 | 单次运行的控制台输出与诊断信息 |

`summary.json.runs[*].engine_logs` 只列该子进程启动前/退出后新增的精确日志路径集合；每个引擎进程正常对应同一 session 后缀的 Engine/Application 两份日志。不要按分钟或 LastWriteTime 猜测矩阵归属。

`Intermediate/` 下的报告是本地生成物，不要提交。

## 5. 报告怎么看

先看 `summary.md` 顶部的整体结果：

| Overall | 含义 |
| --- | --- |
| `PASS` | 没有硬失败，性能趋势也没有超过当前 WARN 阈值 |
| `WARN` | 没有硬失败，但存在性能回归或诊断警告，需要人工判断 |
| `FAIL` | 有硬失败，提交前必须修复 |
| `DRY_RUN` | 只跑了脚本预演，没有实际性能数据 |

然后看每一行 target/backend：

| 列 | 含义 |
| --- | --- |
| `Target` / `Backend` | 当前结果来自哪个程序和图形后端 |
| `Status` | 单项结果：`PASS`、`WARN`、`FAIL` 或 `DRY_RUN` |
| `Baseline` | `MISSING`、`COMPARED`、`NOT_COMPARABLE` 或未比较状态 |
| `Frames` | 采样窗口内统计到的帧数 |
| `CPU Avg ms` | CPU 侧 frame orchestration/submit 平均耗时 |
| `CPU P95 ms` | 95% 帧低于该 CPU 帧耗时，反映常见长尾 |
| `CPU P99 delta` | CPU P99 相对基线的变化 |
| `Private MB` | 进程 private bytes 峰值 |
| `Heap MB` | Engine heap 峰值 |
| `Draw delta` | draw call 平均值相对基线的变化 |
| `GPU Avg/P95 ms` | `GPU.Frame` 主 command buffer 的 timestamp 汇总；不是显示延迟或 Present wall time |
| `GPU coverage` | `valid / submitted`；required metric 另有 `present / submitted` coverage |
| `Adapter` / `Driver` | timing run 的硬件归因元数据 |
| `Actual extent` | 采样窗口内实际 swapchain client extent 与稳定性 |
| `Failures` | 硬失败原因 |
| `Warnings` | 趋势回归或日志警告 |

`CPU Avg/P95/P99 ms` 仍是 CPU 侧帧调度与提交耗时；GPU 结果单独位于 schema v2 的 `gpu`、`gpu_metric_summaries` 与 summary GPU 列。GPU sample 通过原始 renderer frame ID 延迟归档，不能按 poll 顺序与当前 CPU 帧强配对。`summary.json.runs[*].baseline_deltas` 保留每个 CPU/GPU statistic 的 current、baseline、percent delta、实际增量与允许增量；每个 GPU 超限 statistic 也会以含 metric 名的独立文本进入 `warnings` 和 Markdown 的 Warnings 列。

## 6. 和哪些数据对比

性能趋势对比使用同一个基线文件：

```text
tools/perf/perf_gate_baselines.json
```

对比 key 是：

```text
Profile / Configuration / Target / Backend
```

也就是说，`Standard + Debug + Sandbox + Vulkan` 只会和自己的基线比较，不会和 `Editor`、`DX12` 或其他 profile 混比。

Standard 继续使用 baseline 文件里的 legacy 百分比阈值：

| 指标 | WARN 阈值 |
| --- | --- |
| CPU frame time avg / p95 / p99 | `10%` / `15%` / `25%` |
| Process private bytes / Engine heap peak | `15%` / `15%` |
| Draw calls avg | `10%` |

`VegetationFullPipeline` 使用上一节的 larger-of 阈值，并比较全部 11 个 required GPU metric 的 avg/p95。baseline entry 还保存 adapter、driver、OS build、baseline source SHA，以及可读 workload 字段和 SHA-256 fingerprint。workload 包含规范化 repo-relative scene 路径与 scene 内容 SHA-256、extent、fixed_camera、vsync、validation、frame_cap、按 ordinal 排序的 required GPU metric 集合。current source SHA 同样进入 run record，但允许和 baseline source SHA 不同。

对比规则：

| 情况 | 报告表现 |
| --- | --- |
| 找到同 key 且归因相同的基线 | 标 `COMPARED`；显示 delta，超过阈值则逐 metric WARN |
| 没有同 key 基线 | 标 `MISSING` candidate；delta 为 `n/a`，不产生趋势 WARN |
| adapter/driver/OS/fingerprint 不同或缺失 | 标 `NOT_COMPARABLE` 并 FAIL，不做数值比较 |
| baseline 已存在但 required baseline/current metric 缺失、负值或非有限 | fail-closed |
| 基线为 0，当前也为 0 | delta 为 `0.0%` |
| 基线为 0，当前大于 0 | delta 显示 `new`，按新增趋势处理 |

`-TelemetryMode Off` 只用于同配置 A/B，结果标记 GPU baseline 不可比，且仍禁止 bless。

## 7. 什么时候会 FAIL

以下情况属于硬失败：

| 失败类型 | 说明 |
| --- | --- |
| 非 0 退出码 | 目标程序异常退出 |
| Timeout | 超过 profile 的 `timeout_seconds` |
| Telemetry 缺失或格式错误 | 没有产出可解析的性能数据 |
| Backend mismatch | 期望 backend 与实际运行 backend 不一致 |
| 固定运行时契约不匹配 | schema 不是 v2，或 configuration、extent、fixed camera、vsync/validation/frame cap 与 profile 不一致 |
| GPU timing 不完整 | required timing 缺失、coverage 低于阈值、required metric/summary 缺失或 sample count 不一致 |
| 硬件归因缺失 | timing required 时 adapter/driver/backend metadata 缺失或为空 |
| Baseline 不可比 | 固定 profile 的 adapter、driver、OS build 或 workload fingerprint 与 baseline 不同/缺失 |
| 比较合同不完整 | baseline 已存在，但任一 required CPU/GPU statistic 缺失、负值、非有限，或 tier 配置不合法 |
| Validation/debug-layer error | Vulkan validation 或 DX12 debug layer 报错 |
| Engine heap shutdown live bytes 非 0 | 引擎堆在退出时仍有存活分配 |
| Vulkan VMA shutdown live bytes 非 0 | Vulkan VMA 检测到退出时仍有存活分配 |
| Private bytes 超过绝对上限 | 超过 profile 中的进程内存硬上限 |

看到 `FAIL` 时，不应该 bless baseline，也不应该直接提交。先看对应 target/backend 的 stdout/stderr 和 telemetry JSON，定位是启动退出、backend、validation、内存泄露还是指标采集问题。

## 8. 什么时候会 WARN

`WARN` 表示没有硬失败，但需要人工确认：

| WARN 类型 | 处理方式 |
| --- | --- |
| CPU 帧耗时超过阈值 | 判断是否由本次性能敏感改动引入 |
| private bytes 或 Engine heap 峰值上升 | 判断是否是资源常驻、缓存策略或泄露风险 |
| draw calls 上升 | 判断是否是可接受的渲染质量或场景变化 |
| `GPU.Frame` 或 required pass avg/p95 超阈值 | 按 Warnings 中的 metric/statistic 逐项归因；`GPU.Frame` 不会重复进入 pass tier |
| stdout/stderr 出现警告模式 | 优先阅读日志确认是否影响稳定性 |

如果 WARN 是预期变化，并且你确认当前结果是新的可接受状态，可以在修正或确认后运行 `-BlessBaseline` 更新基线。首次 `VegetationFullPipeline` 的 `MISSING` candidate 不是 WARN；仍须先确认机器、驱动、场景、extent、coverage 和 A/B 采集开销，不能自动 bless。

## 9. 如何更新基线

只有在当前结果已经被确认可接受时才更新基线：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
```

`-BlessBaseline` 会把终态为 PASS/WARN、已确认进程树/Job Object 清理且可比性归因完整的记录写入：

```text
baselines.<Profile>.<Configuration>.<Target>.<Backend>
```

固定 profile entry 会同时写 CPU/memory/draw、全部 required GPU avg/p95、adapter/driver/OS build、source SHA、workload fingerprint 与可读 workload 字段。不要用失败、`DRY_RUN`、`NOT_COMPARABLE`、`TelemetryMode Off` 或明显受后台负载影响的结果更新基线；任何 baseline 更新仍需用户明确确认。

当 fresh bless 跨越人工审批窗口会引入不可归因的主机状态漂移时，可改用受保护 report-import。先在同一空闲窗口背靠背采集并审核候选，再锁定被批准 `summary.json` 的精确字节哈希：

```powershell
(Get-FileHash -Algorithm SHA256 Intermediate\test-reports\perf-gate\<run>\summary.json).Hash
```

随后执行上一节的 `-BlessBaselineFromReport` 命令。导入只接受当前仓库报告目录内的 schema v2、未 bless、整体及逐 run 全 PASS 证据，并逐项核对 profile/configuration/baseline path、精确矩阵、当前 source SHA、workload fingerprint、extent/runtime flags、required metrics、coverage、warnings/failures 与进程树/Job cleanup。报告或哈希不符时 baseline 保持原样；成功时 entry 额外持久化 `source_report_sha256`，并在任何构建、GPU 运行或新报告目录创建前退出。导入后仍必须立刻跑一次普通 non-bless profile，确认全部 run 为 `COMPARED` 且无未解释 WARN/FAIL。

## 10. 推荐提交前流程

性能敏感改动完成后：

1. 构建并运行完整门禁：

   ```bat
   RunPerfGate.bat -Profile Standard
   ```

2. 打开最新报告：

   ```text
   Intermediate/test-reports/perf-gate/<timestamp>/summary.md
   ```

3. 如果结果是 `FAIL`，先修复失败项，再重新跑。

4. 如果结果是 `WARN`，确认它是可接受变化还是回归。不可接受就修；可接受时再决定是否更新基线。

5. 如果需要接受新性能状态，运行：

   ```bat
   RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
   ```

6. 提交时不要包含 `Intermediate/` 报告，也尽量避免提交 `product/config/editor/*` 这类本地布局噪声。

## 11. 使用注意事项

- 同一台机器、同一构建配置、同一 backend 下的趋势对比才有意义。
- `Debug` 配置下的 validation/debug-layer 会影响绝对帧耗时，因此不要把 Debug PerfGate 数字直接当成 Release 性能。
- Standard 更适合发现常规趋势回归、内存泄露、backend 错配和 validation 问题；`VegetationFullPipeline` 提供结构化 GPU timestamp 归因。需要单 draw/dispatch、shader wave 或 cache 级诊断时仍使用 RenderDoc、Tracy 或厂商 profiler。
- `summary.md` 是人工入口，`summary.json` 是自动化入口。后续接 CI 时应优先消费 JSON。
