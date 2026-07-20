# ADR-2026-07-13-gpu-driven-instance-runtime: 植被与统一 GPU-driven runtime 的边界

## Status

Accepted（2026-07-13，用户审阅通过）

## Context

AshEngine 当前由 CPU 构建 `VisibleStaticMeshDraw`，执行 CPU 视锥剔除，再按 program、mesh 和 section 合并实例。该路径适合普通场景，但不适合大世界中百万级草木候选、分块流送、GPU LOD/HLOD 和 2560×1440 完整管线 300 FPS 的最终目标。

RHI 已有 Vulkan/DX12 等价的 indirect draw/dispatch，Function 层只有单条 non-indexed indirect draw；RenderGraph 只管理 texture，buffer 状态仍由 program binding 和显式 indirect barrier 旁路处理。GPU 粒子证明了 compute → compact → indirect 的可行性，但其 per-emitter buffer 组织不能直接承担大世界植被或未来统一 GPU Scene。

植被需要专用的作者数据、地形笔刷、确定性散布、物种资产和分块流送；这些能力不应进入通用渲染底座。另一方面，实例页、Prototype/LOD 表、GPU 剔除输入输出和 indirect 命令若做成植被私有格式，未来全流程升级 GPU-driven 时就必须重写。

## Decision

### 1. 分离领域层与运行时底座

系统分成两部分：

- **Vegetation domain**：物种资产、密度层、笔刷、表面采样、确定性烘焙、分块/HLOD 内容和流送策略。
- **GPU-driven instance runtime**：Prototype、实例页、residency、视图输入、剔除/LOD/压实输出、indexed indirect 命令和统计。

Vegetation domain 只向 runtime 提交通用 Prototype 和实例页，不直接创建 Vulkan/DX12 对象，也不拼装后端命令。

### 2. 以数据契约实现未来合流

通用 runtime 使用版本化数据契约，不使用植被类继承体系：

- `GpuDrivenPrototypeId` 标识几何、section、LOD、材质和可选 deformation profile。
- `GpuDrivenInstancePage` 保存 page origin、bounds、容量、generation、transform encoding 和实例 payload。
- `GpuDrivenView` 保存 camera-relative view 数据、frustum、LOD 参数和可选 HZB。
- `GpuDrivenDrawGroup` 保存 prototype/LOD/section 与 visible-list 区间、indirect args 位置。

通用记录不得包含 grass/tree 枚举、笔刷参数或 SpeedTree 类型。植被扩展数据只能通过 flags、deformation profile ID 和 custom-data index 引用。

实例页至少允许两种 transform encoding：植被优先使用紧凑 TRS；未来普通静态网格可先使用完整 affine 3×4，避免层级非均匀缩放产生的 shear 被有损分解。两者共享 page header、residency、剔除输出和 draw contract。

### 3. RenderGraph buffer 成为前置能力

在植被生产渲染之前，RenderGraph 增加 external/transient storage buffer、读写声明、依赖、culling root、lifetime 和 barrier 计划。compute 可见列表、计数器和 indirect args 必须通过 graph 声明，不复制粒子的隐式 buffer 旁路。

首阶段只把 `StorageBuffer` 纳入 graph；geometry vertex/index buffer 仍由 draw binding 管理。该范围足以承载 GPU-driven instance runtime，又避免一次重构所有 buffer wrapper。

### 4. 复用现有 RHI，补齐 Function 契约

RHI 的 `cmd_draw_indexed_indirect` 已是双后端等价能力，GPU-driven runtime 不新增第三套后端接口。Function 层增加显式 indirect kind、indexed indirect、draw count/stride 校验及 `IndirectArgs` graph access。

继续遵守既有跨后端约定：GPU 写入的 `firstInstance` 恒为 0；shader 通过 draw constants 中的 visible-list base 和 Prototype ID 访问压实结果。

### 5. Shader family 面向 GPU-driven surface

生产阶段新增 `SurfaceGPUDriven` engine shader family，继续实现 Material V2 的 Surface/GBuffer/DepthOnly 契约。植被风动作为可选 deformation 输入，而不是建立无法被普通静态网格复用的 `VegetationOnly` 材质域。

### 6. 大世界位置以 page origin 收口

CPU 保存稳定整数分区坐标和 page-local 数据，GPU 使用 camera-relative float。地形与植被共用世界分区坐标和 surface revision 契约，不各自发明全局坐标类型。

### 7. SpeedTree 是离线来源，不是运行时依赖

`.AshVegetation` 是稳定物种资产。首版读取现有 FBX/OBJ/glTF/GLB 与纹理能力；SpeedTree 专用导入器以后在获得 SDK/授权后作为离线适配层加入。Vulkan/DX12 runtime 不链接 SpeedTree SDK。

### 8. 不依赖高端卡专属能力保证正确性

核心路径不要求 bindless、mesh shader、ray tracing 或 indirect-count 扩展。RTX 40xx/50xx 可通过更大的预算和距离提高质量，但 Vulkan/DX12 的基础行为保持等价；可选能力只能作为受 capability flag 门控的优化。

## Consequences

### Positive

- 植被可以按专用路线快速演进，不阻塞于全静态网格迁移。
- 普通静态网格未来通过适配 Prototype/InstancePage 合入同一 runtime，植被数据和 shader contract 不重写。
- RenderGraph 获得第一个有真实需求的 buffer 生命周期消费者，barrier 语义可被测试和验证。
- SpeedTree 授权和地形实现进度不会阻塞基础资产、渲染与测试链。

### Negative

- 在普通静态网格完成迁移前，CPU static-mesh path 与 GPU-driven path 会并存。
- Phase 1 需要先承担 RenderGraph buffer 和 Function indexed indirect 的高风险改动。
- 多 transform encoding 增加 shader/permutation 与测试矩阵。
- 上一帧 HZB、分页 residency 和跨帧回收引入新的 temporal 与 fence 生命周期。

## Rejected alternatives

| Alternative | Reason |
| --- | --- |
| 先重构所有静态网格为 GPU Scene | 回归面覆盖可见性、阴影、拾取、TAA 和实例合批，无法为植被快速交付形成可控阶段 |
| 在现有 CPU batching 上扩展植被 | 每帧 CPU 构造与提交成本无法支撑百万级候选及 300 FPS 最终目标 |
| 植被自建私有 GPU 格式与 barrier | 首版较快，但未来统一 GPU-driven 必须重写，违反已确认的合流要求 |
| 运行时直接链接 SpeedTree SDK | 资产契约、授权和双后端运行时被第三方实现绑定，且当前没有 SDK/资产入口 |

## Migration path

1. 建立 GPU timing 与 Release 性能基线。
2. 增加 RenderGraph buffer、Function indexed indirect 和通用 page/prototype contract。
3. 让 GPU grass 和 GPU tree 成为首批生产消费者。
4. 植被稳定后，以普通静态网格 adapter 逐类迁移到相同 runtime。
5. 最后合并 GBuffer/shadow draw orchestration；Vegetation domain 仍独立保留。

## Invariants

- Editor/Sandbox 不依赖 Graphics 或后端类型。
- Vegetation domain 不包含 Vulkan/DX12 分支。
- `firstInstance == 0`；实例基址走 visible-list base。
- page slot 复用必须检查 generation 并等待相关 GPU frame 完成。
- RenderGraph resource transition 不得发生在 active render pass 内。
- 任何 RHI/RenderGraph 改动必须双后端、validation、RenderGate 和 PerfGate 验证。

## Related documents

- [S3 总体 SDD](../sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md)
- [Phase 0: GPU 性能观测](../sdd/SDD-2026-07-13-gpu-performance-observability.md)
- [Phase 1: GPU-driven foundation](../sdd/SDD-2026-07-13-gpu-driven-foundation.md)
