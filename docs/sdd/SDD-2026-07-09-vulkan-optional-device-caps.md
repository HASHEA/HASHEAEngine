# SDD-2026-07-09-vulkan-optional-device-caps: Vulkan 设备能力分级（必需 / 可选）

## Status
Done(2026-07-09 CI lavapipe 冒烟通过,Vulkan 软渲染冒烟已转阻断;本地全绿:ArchGate / doctest 16/16 / 双后端 Debug 冒烟 / RenderGate 三项 PASS;原则已回写 docs/specs/modules/graphics.md)
（用户 2026-07-09 批准方向："非必需的 ext 查不到就把标记记 false，不允许后面代码死依赖非必需的 ext……不支持了不应该让引擎起不来，而是相关功能不能使用"）

## Context
CI 的 Vulkan/lavapipe 软渲染冒烟在 `VulkanContext.cpp:737` 撞硬断言：
lavapipe（CPU 驱动）主队列族不带 `VK_QUEUE_SPARSE_BINDING_BIT`，引擎直接断言崩溃。
考古结论：全源码 `ASH_TEXTURE_CREATE_FLAG_SPARSE` 零设置调用点、无 `vkQueueBindSparse` 调用——
sparse 是纯未使用的可选能力，却被当成 boot 必需项断言，属能力模型漏网。

## Goals
- 设备能力显式分级：必需集缺失 → 报错优雅退出；可选集缺失 → flag 记 false + 警告日志，引擎继续启动
- sparse binding 从 boot 硬断言降级为可选能力位，检查点从启动挪到使用点
- 原则回写 graphics spec：新可选能力必须 flag 门控，禁止 boot 断言

## Non-goals
- 不实现 sparse 纹理功能本身（当前无调用点）
- 不改 DX12 后端（无对应硬断言）
- 不动其余 10 个已有软开关能力的判定逻辑

## Current implementation
- Entry points: `VulkanContext::_create_device()`（队列选择 + 设备扩展）、`_filter_device_selectable_extension()`、`_query_supported_features()`
- 能力体系：`DeviceExtensionAndFeaturesFlags`（VulkanHelper.hpp）10 项，全部软开关（查不到就不 set bit）
- 必需集（现状，保留）：物理设备存在、graphics+compute 队列族、`VK_KHR_swapchain`、`VK_KHR_shader_draw_parameters`（`shaderDrawParameters` 断言 :910，lavapipe 支持）
- 违例：`:737` 对主队列族 sparse binding 硬断言——唯一把可选能力当必需的位置
- 注意：队列选择循环无 early-out，`mainQueueFamilyIndex` 可能被后续族覆盖，能力位必须在循环后基于最终索引判定

## Proposal

### Module changes
| Module | Change | Files |
| --- | --- | --- |
| Graphics/Vulkan | 枚举加 `SparseBinding` | `VulkanHelper.hpp` |
| Graphics/Vulkan | 删 :737 断言；循环后按最终主队列族 queueFlags 设能力位，缺失 HLogWarning 继续 | `VulkanContext.cpp` |
| Graphics/Vulkan | sparse 纹理创建路径加使用点守卫（能力位断言） | `VulkanTexture.cpp` |
| docs | 能力分级表 + 原则回写 | `docs/specs/modules/graphics.md` |

### API / contract changes
无对外 API 变化。契约新增（spec 固化）：可选能力缺失只允许关闭对应功能，禁止阻断启动；
依赖可选能力的代码必须在使用点检查对应 flag。

### Backend impact
仅 Vulkan。DX12 无 sparse boot 断言（tiled resources 走 DX12Texture 各自路径），无需改动。

### Performance
无。启动期一次位运算 + 可能一条日志。

## Verification plan
| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 双后端构建 | 编译 | `build_editor.bat Debug` / `build_sandbox.bat Debug` |
| 单测 | 回归 | `RunTests.bat Debug` |
| 架构边界 | include 红线 | `RunArchGate.bat` |
| 双后端冒烟 | 启动路径 | `run.bat all Debug --smoke-test-seconds=5` |
| 渲染门禁 | 画面回归 | `RunRenderGate.bat` |
| CI lavapipe 冒烟 | 修复目标 | push 后观察 Actions |

## Task breakdown
1. `VulkanHelper.hpp`：`DeviceExtensionAndFeaturesFlags` 末尾加 `SparseBinding` —— 编译过
2. `VulkanContext.cpp`：删断言、循环后设位/警告 —— 本机（独显）冒烟日志无警告；lavapipe 上应出现警告且继续启动
3. `VulkanTexture.cpp`：sparse 分支加 `H_ASSERTLOG` 能力位守卫 —— 编译过（无现存调用点触发）
4. spec 回写 + SDD 归档

## Risks
| Risk | Mitigation |
| --- | --- |
| lavapipe 冒烟通过此关后暴露下一个不兼容点 | 预期迭代，冒烟仍为 experimental（continue-on-error） |
| 未来新增 sparse 使用点忘记查 flag | 使用点守卫 + spec 原则条目 |

## Open questions
- 无

## 执行发现(2026-07-09,lavapipe 冒烟第二轮)
sparse 修复后 CI 推进到 buffer 创建,暴露同类问题两个:
1. `VulkanBuffer::create` 断言"实际内存 coherent 必须等于预期"——UMA/软件驱动上所有内存
   都是 HOST_COHERENT,GPU_ONLY buffer 落在 coherent 内存属正常。改为仅预期 host 访问时
   做持久映射,不再把环境差异当错误。
2. `hlog.h` 的 `ASH_LOG_PROCESS_ERROR`/`ASH_PROCESS_ERROR_EXIT` 格式串 4 个占位符但固定
   实参 3 个,不带附加消息调用(98 处)失败时 fmt 炸 "argument not found",真实报错被吞
   (本次排障即受害)。改为附加消息按相邻字符串字面量拼接进格式串。
