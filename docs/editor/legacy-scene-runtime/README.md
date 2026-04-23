# Legacy Scene Runtime Archive

这套 `Scene / Entity / Components / SceneSerializer` 代码来自编辑器早期自持运行时方案。

当前主线编辑器已经改为直接依赖引擎侧 `Function/Scene/*` 与 `UIContext`，因此这组文件不再属于活跃源码，也不应再放回 `project/src/editor` 的编译路径。

保留这份归档的目的只有两个：

- 给后续排查历史设计时做参考
- 解释为什么 `premake` 不再排除 `project/src/editor/Scene/**`

当前规则：

- 不要在新功能里引用这组文件
- 不要把它们重新移回 `project/src/editor`
- 如果需要重用其中思路，应先在文档里记录原因，再按当前 editor / engine 边界重新实现
