# 0.1.12 每节点再省 8 字节

延续 0.1.11 的名字压缩，做了上一轮列出的第 1、2 项。

## 1. 去掉 `m_childCount`

子节点改成标准 CSR：`m_childStart` 有 `count+1` 个偏移，节点 i 的子节点就是
切片 `[start[i], start[i+1])`。数量由两个偏移相减得到，不再单独存每节点一个
`int`。

- 省：每节点 4 字节（300 万约 12 MB）。
- 无行为差异；`childCountOf` / `childAt` 的语义不变，所有遍历代码未改。

## 2. `modified` 改存 32 位 Unix 秒

时间列只显示到分钟，FILETIME 的 100 纳秒精度没有用武之地。

- `FlatTree.modified` 从 `qint64` 改为 `quint32`（Unix 秒，0 表示无）。
- 加载时用 `unixSecondsFromFileTime()` 转换；已有数据、快扫、fixture 三条
  来源统一走这一处。
- 显示改用 `QDateTime::fromSecsSinceEpoch`。
- 省：每节点 4 字节（300 万约 12 MB）；32 位秒可表示到 2106 年。

## 合计

- 名字表（0.1.11）：约 108 MB / 300 万。
- 本轮：约再省 24 MB / 300 万。
- 相比改造前（QStringList + 64 位 FILETIME + childCount）总共约省
  140–180 MB。

## 验证

- 模型单测（含目录展开/折叠、选择、时间列格式化）、扫描单测、真实扫描自检、
  Dock 16 次启动检查通过。
- 200k 合成树：`enter=15 ms`、`reset=48 ms`、`export=5 ms`，与 0.1.11 持平。
- 未在用户当前 WinShun 上实测内存。
