# ADR 0003: LMK 阈值调整策略 —— 前台流畅优先，兼顾后台保活

## Status

accepted

## Context

内核使用旧式 `lowmemorykiller`（`drivers/staging/android/lowmemorykiller.c`），默认 `lowmem_adj={0,1,6,12}`、`lowmem_minfree={6,8,16,64}MB`（`:60-75`）。设备 4GB RAM，主要痛点之一是后台进程频繁被杀导致的「杀-重建循环」卡顿。

## Decision

通过调整 `lowmemorykiller` 的 `minfree`/`adj` 数组，**让后台进程更早、更平滑地被请走，避免挤占前台内存**，优先保障前台流畅，同时用 `lmk_multi_kill` 控制批量杀进程的节奏，兼顾后台保活不至于过度激进。

## Considered Options

- **保持默认（激进保活）**：`minfree` 偏低，前台可用内存紧张时才杀后台，容易造成回收尖峰与「杀-重建循环」。
- **大幅上调 minfree**：可提前释放内存、前台更稳，但会过于频繁杀后台，影响多任务切换体验。

## Consequences

- 具体阈值以「前台流畅优先、后台不过度被杀」的折中为准，需在设备上实际验证后固化。
- 该值可通过 `/sys/module/lowmemorykiller/parameters/{adj,minfree}` 运行时调整，便于快速回归，无需改代码。
