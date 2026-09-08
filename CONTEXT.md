# Context — Hi3660 / Kirin 960 内核性能与内存优化

> 本文件是**术语表**（glossary），只定义项目语言，不含实现细节。
> 领域：Linux 4.9 内核在华为麒麟 960（Hi3660）平台上的性能/功耗/内存调优。
> 设备基线：4GB RAM，EMMC 5.1，EMUI 9.1.0 + HarmonyOS 2.0 UI。

## 性能 / 卡顿

- **卡顿（Stutter）** — 用户可感知的掉帧、延迟或停顿。与「性能不足」不同，卡顿更常源于调度/内存/IO 的**瞬时尖峰**，而非持续负载无能为力。
- **前台（Foreground）** — 当前正在与用户交互的应用/场景，优先保障其流畅与响应。
- **后台（Background）** — 非当前交互的应用/进程，可被降级、回收或冻结。
- **前后台平衡** — 在「前台流畅度」与「后台保活」之间权衡：既要前台不掉帧，又要后台不频繁被杀重建。
- **杀-重建循环（Kill-Recreate Loop）** — 后台进程被杀后，用户再次切换到它时重新冷启动，造成的卡顿/黑屏/加载。是 4GB 设备上「内存压力导致卡顿」的典型表现。
- **久用衰减（Long-Use Degradation）** — 设备使用一段时间后越来越卡，通常源于内存碎片、泄漏或缓存策略随时间恶化。

## 调度（CPU）

- **调度器（Scheduler）** — 决定 CPU 时间片如何分配给线程的内核组件。
- **big.LITTLE** — 大小核异构架构：麒麟 960 = 4×A73（大核）+ 4×A53（小核），共 8 核。需把负载按特性放到合适的核上。
- **EAS（Energy Aware Scheduling）** — 基于能量模型的省电调度，把任务放到「性能/功耗最优」的核上。
- **schedtune（Sched Tune / Stune）** — 为不同任务组（如前台、后台、top-app）定义性能偏好与提升（boost）程度的机制。
- **WALT（Window-Assisted Load Tracking）** — 基于窗口的负载跟踪模型，常用于 Android 调度以改善交互性能。
- **cpufreq / governor** — CPU 频率调整机制；governor（如 schedutil、blu_schedutil、interactive）决定何时升降频。
- **CPU 输入升频（Input Boost / CPU Boost）** — 检测到触摸输入后短暂提升 CPU 频率，以降低输入到响应的延迟。

## GPU

- **DVFS（Dynamic Voltage and Frequency Scaling）** — 根据负载动态调整 GPU/CPU/DDR 频率电压。
- **devfreq** — 内核里通用设备频率调节框架，GPU 也通过它调频。
- **GPU governor** — 决定 GPU 升降频策略的组件（如 `gpu_scene_aware`、`mali_ondemand`）。
- **硬件投票（HW Vote）** — 让 CPU/GPU/DDR 之间通过硬件统一协商频率的机制，通常比纯软件调频更协同。

## 内存

- **内存压力（Memory Pressure）** — 可用内存不足、回收频繁的状态，常导致卡顿或杀后台。
- **内存回收（Reclaim）** — 内核回收可释放页面的机制，含直接回收（synchronous）与后台回收（kswapd）。
- **swappiness** — 倾向在内存回收时把匿名页换出（swap）的比例倾向，值越高越倾向换出而非丢弃文件页。
- **zram** — 把换出的匿名页**压缩**后存放在**内存**中的交换后端（无磁盘 IO，但占用内存）。
- **zswap** — 在写回磁盘前**先压缩缓存**的交换后端（写回慢，但省内存与磁盘 IO）。
- **LMK / lowmemorykiller（Low Memory Killer）** — Android 内核里按 `oom_score_adj` 阈值杀死后台进程以释放内存的机制。
- **CMA（Contiguous Memory Allocator）** — 预留一段可回收的连续内存，供需要物理连续内存的设备（如 GPU、相机）使用。
- **ION** — Android 的物理连续/大块内存分配器，常用于 GPU/相机 buffer。
- **THP（Transparent Huge Pages）** — 透明大页，减少页表开销与缺页次数。
- **KSM（Kernel Samepage Merging）** — 合并相同内容的匿名页以省内存。

## 存储 / IO

- **IO 调度器（I/O Scheduler）** — 决定读写请求如何排序/合并，影响随机读写延迟。
- **EMMC 5.1** — 该设备的存储介质类型；随机读写延迟较高，是 IO 类卡顿的硬件根源。
