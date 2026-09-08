# ADR 0001: 保留 Hisi 自研 EAS + schedtune，不开启 SCHED_WALT / HISI_RTG

## Status

accepted

## Context

麒麟 960（Hi3660）在 Linux 4.9 上使用华为自研 EAS 路径：`CONFIG_HISI_EAS_SCHED=y` + `CONFIG_SCHED_TUNE=y` + `CONFIG_DYNAMIC_STUNE_BOOST=y`。`CONFIG_SCHED_WALT` 被关闭（`defconfig:102`），这同时使它依赖的 `HISI_RTG`（`init/Kconfig:1454`）不可用。

## Decision

在本次卡顿优化中，**保留并调优现有的 Hisi EAS + schedtune 路径**，不开启 `SCHED_WALT` / `HISI_RTG`。

## Considered Options

- **开启 WALT**：可解锁窗口负载跟踪与 RTG 线程组，理论上对交互/换频更精准，但需要与已有 Hisi EAS 集成，在定制内核上有较高回归风险。
- **维持 EAS**：避免引入双调度路径冲突，收益集中在 schedtune boost 与 unplug 策略调参上，风险低。

## Consequences

- 调度优化选择 schedtune 前台 boost、大核 unplug 策略、`blu_schedutil` 参数与 `CPU_BOOST` 调参。
- 若未来强烈需要 WALT 的交互优势，需重新评估并可能构成对 ADR 0001 的修订。
