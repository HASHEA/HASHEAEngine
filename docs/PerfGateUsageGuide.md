# PerfGate 性能门禁使用说明

PerfGate 是性能敏感改动提交前的标准检查流程。它会自动运行指定目标程序，采集帧耗时、FPS、进程内存、Engine heap、后端内存、draw/pass/dispatch 数量和日志诊断，并生成可追溯报告。

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

只验证脚本流程和报告生成，不启动目标程序：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -DryRun
```

把当前通过门禁的结果写成新基线：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
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
| `Frames` | 采样窗口内统计到的帧数 |
| `CPU Avg ms` | CPU 侧 frame orchestration/submit 平均耗时 |
| `CPU P95 ms` | 95% 帧低于该 CPU 帧耗时，反映常见长尾 |
| `CPU P99 delta` | CPU P99 相对基线的变化 |
| `Private MB` | 进程 private bytes 峰值 |
| `Heap MB` | Engine heap 峰值 |
| `Draw delta` | draw call 平均值相对基线的变化 |
| `Failures` | 硬失败原因 |
| `Warnings` | 趋势回归或日志警告 |

注意：当前 `CPU Avg/P95/P99 ms` 是 CPU 侧帧调度与提交耗时，不是 GPU 执行耗时。GPU timestamp query 后续应写入独立的 `gpu_frame_time_ms` 字段。

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

当前会和基线比较的主要指标：

| 指标 | WARN 阈值 |
| --- | --- |
| CPU frame time avg | `10%` |
| CPU frame time p95 | `15%` |
| CPU frame time p99 | `25%` |
| Process private bytes peak | `15%` |
| Engine heap peak | `15%` |
| Draw calls avg | `10%` |

对比规则：

| 情况 | 报告表现 |
| --- | --- |
| 找到同 key 基线 | 显示百分比 delta，超过阈值则 WARN |
| 没有同 key 基线 | delta 显示 `n/a`，不产生趋势 WARN |
| 基线为 0，当前也为 0 | delta 为 `0.0%` |
| 基线为 0，当前大于 0 | delta 显示 `new`，按新增趋势处理 |

## 7. 什么时候会 FAIL

以下情况属于硬失败：

| 失败类型 | 说明 |
| --- | --- |
| 非 0 退出码 | 目标程序异常退出 |
| Timeout | 超过 profile 的 `timeout_seconds` |
| Telemetry 缺失或格式错误 | 没有产出可解析的性能数据 |
| Backend mismatch | 期望 backend 与实际运行 backend 不一致 |
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
| stdout/stderr 出现警告模式 | 优先阅读日志确认是否影响稳定性 |

如果 WARN 是预期变化，并且你确认当前结果是新的可接受状态，可以在修正或确认后运行 `-BlessBaseline` 更新基线。

## 9. 如何更新基线

只有在当前结果已经被确认可接受时才更新基线：

```bat
RunPerfGate.bat -Profile Standard -SkipBuild -BlessBaseline
```

`-BlessBaseline` 会把非 `FAIL`、非 `DRY_RUN` 的记录写入：

```text
baselines.<Profile>.<Configuration>.<Target>.<Backend>
```

不要用失败结果更新基线。也不要把一次明显受本机后台负载影响的异常结果 bless 成基线。

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
- 当前门禁更适合发现明显回归、内存泄露、backend 错配和 validation 问题；精细 GPU 性能分析仍应使用 RenderDoc、Tracy 或后续 GPU timestamp telemetry。
- `summary.md` 是人工入口，`summary.json` 是自动化入口。后续接 CI 时应优先消费 JSON。
