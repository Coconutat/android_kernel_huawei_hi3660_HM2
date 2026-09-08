# ADR 0002: 维持 zram，不引入 zswap

## Status

accepted

## Context

当前内核已启用 `CONFIG_ZRAM=y`（`defconfig:1457`），`CONFIG_ZSWAP` 未启用。设备为 4GB RAM + EMMC 5.1（存储为对象，无独立 SSD 控制器，随机读写延迟较高）。

## Decision

针对「卡顿 + 内存压力」的目标，**维持 zram 作为交换后端**，不引入 zswap。

## Considered Options

- **维持 zram**：把换出的匿名页压缩后存放在内存中，无磁盘写回；对 EMMC 5.1 最友好，避免写放大与磨损。
- **引入 zswap**：在写回磁盘前先压缩缓存，更省物理内存，但会增加磁盘写回（EMMC 5.1 上换出路径更慢）。

## Consequences

- 内存回收走 zram 路径，swappiness 调优需与压缩后端配合（见优化项 M3）。
- 若未来内存压力成为瓶颈且可接受 EMMC 写放大，可再评估 zswap，届时构成对 ADR 0002 的修订。
