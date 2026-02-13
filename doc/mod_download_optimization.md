# MOD下载功能优化方案

## 1. 现状分析

当前 `ModDownloadPage` 的下载逻辑采用串行方式（递归调用 `downloadNextInQueue`），即下载完一个文件后才开始下载下一个。
**存在问题**：

- **效率低**：在下载包含大量依赖的模组或整合包时，带宽利用率低，耗时较长。
- **稳定性差**：缺乏自动重试机制，单个文件因网络波动下载失败会导致整个流程中断或需要人工干预。
- **UI反馈**：进度条更新逻辑简单，且在失败时缺乏友好的提示。

## 2. 优化目标

1.  **提高下载速度**：实现多文件并发下载。
2.  **增强稳定性**：增加下载失败自动重试机制。
3.  **优化用户体验**：优化进度条显示，确保并发状态下进度反馈准确。

## 3. 技术方案

### 3.1 并发下载机制

- **任务队列**：复用 `m_downloadQueue` 作为待处理任务队列。
- **并发控制**：
  - **利用现有的全局设置**：使用 `APPLICATION->settings()->get("Threads")` 获取用户配置的下载线程数，替代固定的常量。
  - **利用 `NetJob` 的内置并发**：将所有下载任务添加到一个 `NetJob` 中，而不是为每个文件创建单独的 `NetJob`。`NetJob` 类内部已经实现了基于全局线程设置的并发队列管理。
- **调度逻辑**：
  - 废弃递归调用的 `downloadNextInQueue`。
  - 重构 `processDownloadQueue`，构建包含所有模组下载任务的单一 `NetJob`。

### 3.2 失败重试机制

- **复用 `NetJob` 机制**：`NetJob` 内部已经实现了每个分片任务失败后的自动重试（默认 3 次）。无需额外手动实现重试逻辑。
- **处理逻辑**：
  - 监听 `NetJob` 的 `partSucceeded` 和 `partFailed` 信号，分别处理单个模组下载成功或彻底失败的情况。

### 3.3 进度条与状态管理

- **状态变量**：
  - 新增 `m_totalFiles`：记录开始下载时的总文件数（用于计算进度分母）。
  - 新增 `m_finishedFiles`：记录成功或彻底失败的文件数。
- **进度更新**：
  - `progressBar->setValue((m_finishedFiles * 100) / m_totalFiles)`。
  - 注意：并发下载时，进度条可能不会像串行那样平滑（多个任务同时进行中），但整体进度是准确的。

## 4. 代码变更计划

### 4.1 修改 `ModDownloadPage.h`

- 添加 `DownloadItem` 的 `retryCount` 字段。
- 添加成员变量：`m_activeDownloads`, `m_totalFiles`, `m_finishedFiles`。
- 添加常量定义。
- 声明 `scheduleDownloads()` 和 `onAllDownloadsFinished()`。

### 4.2 修改 `ModDownloadPage.cpp`

- 重构 `processDownloadQueue`：初始化变量，调用 `scheduleDownloads`。
- 重构 `downloadSingleItem`：
  - 移除递归的 `onComplete` 回调参数，改为内部调用 `scheduleDownloads`。
  - 实现成功和失败的处理逻辑（重试、更新计数）。
- 实现 `onAllDownloadsFinished`：处理批量写入 JSON 和 UI 恢复。

## 5. 预期效果

- 下载耗时显著减少（取决于并发数和带宽）。
- 偶发的网络错误被自动处理，用户感知到的成功率提高。
