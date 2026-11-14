# 0.1.7 快扫直出扁平数组

用户报告：C: 快扫读完后会再跑一遍普通扫描，进程持续写盘。

## 根因

快扫读完 MFT 后调用 `mft::build` 建目录索引，期间同时存在：

- MFT 读取的全部条目与文件名；
- `sizes` / `modifieds` / `children` / `directoryAttributes` 四张按记录分配的哈希表；
- 结果 `Node` 树；
- 随后 `flatten` 再建一套 `FlatTree` 并行数组。

C: 全量到达内存上限时 `bad_alloc`，旧实现把它当成“回退”，于是接着做普通遍历。普通遍历与残留数据叠加，造成持续写盘（页面文件换出）。

## 修复

- 新增 `fastscan::buildFlat`：从 MFT 条目直接写 `FlatTree`。
  - 一条 `TempNode` 对应一个记录，文件名从读取批次移动而来。
  - 记录号直接索引父节点，不再使用逐记录哈希表。
  - 子节点用 CSR（计数、前缀和、填充）组织，无逐节点 `vector`。
  - DFS 按父先子后输出，同时构建完整路径用于排除目录。
  - 一次倒序累加得到目录的文件数、文件夹数、大小和已分配空间。
- `Outcome::MemoryFailure` 与 `Fallback` 分开：
  - `Fallback`（非固定 NTFS 根、卷打不开、记录损坏）才转普通遍历。
  - `MemoryFailure` 直接报告“内存不足”，不再启动第二遍扫描。
- 快扫成功后跳过 `Node` 树与 `flatten`，`ScanService` 直接使用扁平结果。

## 验证

- 扫描单测新增 `testFlatBuild`：父先子后顺序、根合计、目录大小、嵌套名称、排除子树及排除后合计；通过。
- 扫描单测、模型自检、插件 DLL 与独立程序构建通过。
- 未在用户当前 WinShun 上实际跑 C: 全盘；需安装后复测读取阶段、完成阶段和进程写盘。
