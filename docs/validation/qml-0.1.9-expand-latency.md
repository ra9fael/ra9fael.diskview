# 0.1.9 双击展开/进入目录的响应

用户反馈：树里双击展开子目录、双击可视化元素进入子目录都很耗时。

## 原因

- 展开一个目录走 `applyRowChange`：`beginResetModel/endResetModel` 重建全部可见行，
  ListView 随之重建所有委托；展开一个节点却付了整表的代价。
- 进入目录同样整表 reset，即使可见行完全没变（目录本来就是打开的）。
- 图表用整棵嵌套 `QVariantMap` 判断数据是否变化：`m_treeData == data` 是
  数千个 map 的深比较，开销接近重新解析一次。

## 修改

1. `toggleExpanded` 在无筛选的树视图下改为增量：
   - 展开只在该目录后插入它的子块（`beginInsertRows`），子块按当前排序递归；
   - 折叠只移除紧随其后的后代块（`beginRemoveRows`）。
2. `resetAt` 先把新行构建到临时数组，与当前行相同就**不 reset**；
   只有可见行真的变化时才重置模型。
3. 行构建抽成 `buildRows(QVector<int>&)`，重置与比较共用同一实现。
4. 图表数据带一个 `revision` 整数：`ChartItem` 只比较这个整数，不再深比较整棵树；
   `revision` 随 `chartChanged` 递增。

## 验证

- 模型单测新增：展开后子节点紧随父目录、折叠只移除该块、重复切换不重复行、
  导航推进 chart revision；通过。
- 模型测试、真实扫描自检、Dock 16 次启动检查、两种图表截图检查通过。
- `--bench-chart 200000`：导出约 158ms/3 次，解析约 5ms；剩余主要成本是导出
  嵌套 `QVariantMap`，未在本轮重构。
