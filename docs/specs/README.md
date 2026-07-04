---
owner: huyizhou
last_reviewed: 2026-07-04
status: active
---

# Spec 基线

长期规格文档：描述**当前现状**（行为、接口、约束、验证方式），不记录计划与历史过程。
与变更流程的关系构成三层结构：

| 层 | 位置 | 生命周期 | 内容 |
| --- | --- | --- | --- |
| 模块 spec | `docs/specs/modules/` | 长期，随代码演进 | 模块职责边界、公共接口、约束、验证 |
| feature spec | `docs/specs/features/` | 长期，随代码演进 | 单个功能的行为、配置、实现要点、验证 |
| 变更 SDD | `docs/sdd/` | 一次性 | 驱动一次 S1+ 变更；Done 后把结论回写到对应 spec |

历史设计/实施文档在 `docs/superpowers/`，**只作归档，不可当现状依据**；现状以本目录 spec 与代码为准，冲突时以代码为准并修 spec。

## 维护规则

- 改代码改到 spec 描述的行为/接口/配置时，同一次提交内更新 spec。
- SDD 关闭（Status: Done）时，把最终结论回写对应 spec，SDD 本身不再维护。
- spec 保持薄：单文件目标 ≤ 150 行；能从代码一眼看出的细节（参数默认值全表、私有实现）不抄进 spec。
- 新增 feature 必须新增或扩充对应 feature spec；新增模块必须新增模块 spec，并更新本索引。

## 索引

### 模块 spec

| Spec | 覆盖 |
| --- | --- |
| [base.md](modules/base.md) | 平台无关基础设施（日志/内存/窗口/输入/ds/文件/线程/ini） |
| [graphics.md](modules/graphics.md) | RHI 抽象 + Vulkan/DX12 后端 + DXC shader 编译 |
| [render.md](modules/render.md) | SceneRenderer 帧编排、pass 组织、SceneRenderConfig |
| [render-graph.md](modules/render-graph.md) | RenderGraph API 契约（原 docs/RenderGraphAPISpec.md 迁入） |
| [scene.md](modules/scene.md) | 逻辑场景（Entity/组件/变更事件）与 ScenePresentationSubsystem |
| [asset.md](modules/asset.md) | AssetDatabase 资产加载 |
| [application.md](modules/application.md) | Application 生命周期、EntryPoint 命令行、UIContext |
| [editor.md](modules/editor.md) | Editor 壳（App/Core/Shell/Panels/Services/Widgets） |
| [sandbox.md](modules/sandbox.md) | Sandbox 验证程序与测试注册 |
| [tools.md](modules/tools.md) | 门禁与工具链（PerfGate/RenderGate/AshImageDiff/AIDevDoctor） |

### Feature spec

| Spec | 覆盖 |
| --- | --- |
| [deferred-lighting.md](features/deferred-lighting.md) | GBuffer（DeferredHQ）+ deferred lighting 子 pass 族 |
| [shadows.md](features/shadows.md) | Sunlight CSM + 普通方向光 shadow（两条路径） |
| [ambient-occlusion.md](features/ambient-occlusion.md) | SSAO / HBAO / GTAO |
| [skybox-ibl.md](features/skybox-ibl.md) | 环境光照（IBL 烘焙链）+ 天空背景 |
| [volumetric-lighting.md](features/volumetric-lighting.md) | Froxel 体积光 + 屏幕空间 fallback |
| [bloom.md](features/bloom.md) | 多级 Bloom |
| [taa.md](features/taa.md) | Temporal AA（含抓帧模式禁 jitter 约定） |
| [tonemap.md](features/tonemap.md) | HDR→LDR tone mapping 输出 |
| [material-system.md](features/material-system.md) | 材质 V2（.AshMat/.AshMatIns、Domain/Family shader 拼合） |
| [scene-config.md](features/scene-config.md) | scene json 的 `scene_config` 渲染配置 schema |
| [render-debug-view.md](features/render-debug-view.md) | 中间纹理调试可视化 |
| [debug-draw.md](features/debug-draw.md) | DebugDrawService 调试几何绘制 |

## 模板

### 模块 spec 模板

```markdown
# Module Spec: <名称>

## 职责与边界
<管什么、不管什么，一段话>

## 目录与关键文件
| 路径 | 内容 |
| --- | --- |

## 公共接口
<对外类型/函数/契约要点；上层只应依赖这里列出的东西>

## 约束与不变式
<依赖方向、线程、生命周期、双后端等价等硬规则>

## 验证
<改本模块需要跑的命令（对齐 docs/VERIFY.md 变更矩阵）>

## 历史
<相关 SDD / superpowers 归档链接；可为空>
```

### Feature spec 模板

```markdown
# Feature Spec: <名称>

## 行为
<效果与帧管线中的位置（上下游 pass、输入输出）>

## 配置
<scene_config / Engine.ini 关键字段：名称、含义、默认；不必全表>

## 实现
<pass 类与文件、shader 文件、关键算法一句话>

## 约束与已知限制

## 验证
<RenderGate / RenderDebugView / 专项验证方法>

## 历史
<来源 SDD / superpowers 归档链接>
```
