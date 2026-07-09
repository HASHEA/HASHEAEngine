# Contributing

面向所有协作者的仓库工作流说明。AI/引擎开发规则见 `AGENTS.md`,文档路由见 `docs/README.md`。

## 分支模型

`main` 受分支保护,**所有变更走 PR**,不能直推。日常流程:

```bash
git checkout -b my-change
# ... 改动、本地验证 ...
gh pr create -f
gh pr merge -m --delete-branch    # 满足合并条件后
```

## PR 合并条件(两档)

| 改动路径 | 条件 |
| --- | --- |
| 普通路径 | CI 绿即可**自助合并**,不需要任何人 approve |
| CODEOWNERS 圈定路径(见下) | CI 绿 + **@HASHEA approve** |

CODEOWNERS(`.github/CODEOWNERS`)圈定的是 CI 与门禁基建:
`.github/`、根目录入口脚本(`RunArchGate.bat` / `RunTests.bat` / `RunRenderGate.bat` /
`RunPerfGate.bat` / `generate_vs2022.bat` / `build_editor.bat` / `build_sandbox.bat` /
`run.bat`)、`scripts/`、`tools/ai-dev/`。

## CI

- 触发:push 到 `main` 和所有 PR;**纯文档改动**(`docs/**`、`**.md`)不触发
- 内容:ArchGate(架构边界)→ 生成/构建(Debug + Release)→ doctest 单测 →
  双后端软渲染冒烟(DX12/WARP 阻断;Vulkan/lavapipe 实验性)
- 新 push 不打断正在跑的 run(排队,过期的待跑 run 自动作废)
- runner 无 GPU:RenderGate / PerfGate 不在 CI 里,渲染与性能改动**必须本地跑门禁**
  (见 `docs/VERIFY.md` 验证矩阵)

## 基线文件

`tools/render/goldens/`(渲染 golden)与 `tools/perf/perf_gate_baselines.json`(性能水位)
对协作者开放,但**禁止手改**——必须走 bless 流程:确认画面/水位正确后
`RunRenderGate.bat -BlessGolden` / `RunPerfGate.bat -BlessBaseline`。

## 其他

- 提交规范、SDD 风险分级(S0-S3)、架构红线:`AGENTS.md`
- 本地验证矩阵:`docs/VERIFY.md`
