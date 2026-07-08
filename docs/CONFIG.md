---
owner: huyizhou
last_reviewed: 2026-07-08
review_cycle: monthly
status: active
---

# CONFIG: Engine.ini 配置项权威文档

`product/config/Engine.ini` 全部配置项的唯一权威说明。**新增/修改配置项必须同步本文**。
改动 Engine.ini 属高危路径（AGENTS.md）：需双后端 smoke + 日志确认开关生效。

## 加载机制

- 路径：`product/config/Engine.ini`，相对仓库根解析（可执行程序启动时 cwd 被重置到仓库根，`resolve_runtime_config_path`）
- 文件缺失 / 键缺失：使用编译期默认值（各节下表），并打 `HLogInfo` 提示；非法值打 `HLogWarning` 并保留默认
- 优先级：命令行 `--rhi=<vulkan|dx12>` > ini（RenderGate 用命令行切后端，不动 ini）；`run.bat <target> <backend>` 通过临时改写 ini 完成切换、退出后还原
- 读取入口：`RHI` 与 validation 节 `Graphics/DynamicRHI.cpp: load_runtime_rhi_config`；其余各节由对应模块的 `load_runtime_*_config` 读取（下表标注）

## [RHI]

读取：`Graphics/DynamicRHI.cpp`

| Key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `Backend` | string | 编译默认（Windows：DX12 > Vulkan） | 接受 `vulkan`/`vk`、`dx12`/`directx12`/`d3d12`、`default`；非法值警告并回退编译默认；请求的后端未编译时回退并警告 |

## [Rendering]

读取：`Function/Render/RenderFeatureConfig.cpp`（render switch 描述表驱动，目前仅 1 项）

| Key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `VSync` | bool | `false` | 垂直同步；`Application` 启动时读取并传给交换链 |

## [VulkanValidation]

读取：`Graphics/DynamicRHI.cpp`；结构体默认见 `Graphics/GraphicsContext.h: VulkanValidationConfig`

| Key | 类型 | 默认（Debug / 非 Debug） | 说明 |
| --- | --- | --- | --- |
| `Enabled` | bool | `true` / `false` | Vulkan validation layer 总开关 |
| `GpuAssisted` | bool | `false` / `false` | GPU-assisted validation |
| `SynchronizationValidation` | bool | `false` / `false` | 同步 validation；**放大 CPU 帧时间 5-10x**，按需 opt-in |
| `BreakOnValidationError` | bool | `true` / `false` | validation 报错时断点 |

注意：非 Debug 构建下 Vulkan validation 不被强制关闭（可经 ini 打开），DX12 则会（见下节）。

## [DX12Validation]

读取：`Graphics/DynamicRHI.cpp`；结构体默认见 `Graphics/GraphicsContext.h: DX12ValidationConfig`

| Key | 类型 | 默认（Debug / 非 Debug） | 说明 |
| --- | --- | --- | --- |
| `Enabled` | bool | `true` / `false` | D3D12 debug layer 总开关 |
| `GpuValidation` | bool | `true` / `false` | GPU-based validation |

**非 Debug 构建两项被代码强制关闭**（`DynamicRHI.cpp` `#if !defined(ASH_DEBUG)`），ini 设置无效。

## [EnvironmentLighting]

读取：`Function/Render/EnvironmentMapAsset.cpp: load_runtime_environment_lighting_config`；
默认见 `EnvironmentMapAsset.h: EnvironmentLightingConfig`。尺寸类键要求 2 的幂，越界/非 2 幂时保留默认并警告。

| Key | 类型 | 默认 | 合法范围 | 说明 |
| --- | --- | --- | --- | --- |
| `RuntimeBakeCache` | bool | `true` | — | 运行时 IBL bake 结果落盘缓存 |
| `DefaultRadianceSize` | uint | `1024` | 2 的幂，[1, 4096] | radiance 环境图尺寸 |
| `DefaultIrradianceSize` | uint | `64` | 2 的幂，[1, 1024] | irradiance 图尺寸 |
| `DefaultPrefilterSize` | uint | `256` | 2 的幂，[1, 2048] | prefilter 环境图尺寸 |
| `DefaultPrefilterMipCount` | uint | `8` | [1, PrefilterSize 的最大 mip 数] | prefilter mip 链长度 |
| `DefaultBRDFLUTSize` | uint | `256` | 2 的幂，[1, 1024] | BRDF LUT 尺寸 |
| `DefaultSampleCount` | uint | `256` | [1, 4096] | bake 采样数（当前 ini 设为 1024） |

## [RenderDebugView]

读取：`Function/Render/RenderDebugView.cpp: load_runtime_render_debug_view_config`；
默认见 `RenderDebugViewConfig.h`。

| Key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `Enabled` | bool | `false` | 是否显示 "Render Debug View" 调试面板（当前 ini 设为 `true`） |
| `Selected` | string | `Off` | 选中的调试通道名；`Off` = 关闭。通道名由 SceneRenderer 每帧动态注册（GBuffer 通道/AO/阴影等），以面板下拉列表为准；空串回退 `Off`，未知名等效 `Off` |

## 维护规则

- 新增配置项：代码 + Engine.ini 示例 + 本文表格，同一提交
- 配置行为变化（默认值、范围、语义）：同步更新本文对应行
- 验证：按 `docs/VERIFY.md` 的 `product/config/Engine.ini` 行执行（双后端各 smoke 一次 + 日志确认开关生效）
