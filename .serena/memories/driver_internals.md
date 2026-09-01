# Driver Internals

Complements `AGENTS.md` (architecture notes / gotchas). Only non-obvious contracts here.

## I2C wrapper + status model (`apds9960.c:31-64`)

`rd`/`wr`/`rdBlock` are 1:1 over `i2c_read_register`/`i2c_write_register`/`i2c_read_buffer`, plus one side effect: on failure they store the raw `i2c.h` code in `g_last_i2c_status`. Two separate error channels, deliberately:

- `g_last_error` (`APDS_ERR_*`) — set by the *callers* of `rd`/`wr`, reset to `APDS_ERR_NONE` on success of most public calls.
- `g_last_i2c_status` (raw `I2C_*`) — write-only-on-failure, **never cleared**. Read it only when `apds_getLastError() == APDS_ERR_I2C`; otherwise it is a stale historical value. Do not "fix" this by clearing it on success — it is the documented contract in `apds9960.h`.

No retry inside the wrappers. A single failed register access propagates `false` upward immediately.

## Fault-tolerance policy (asymmetric, intentional)

- `calibrate_proximity()` — I2C fault during *sampling* sets a local `aborted` flag, breaks the loop, and falls through; too few valid samples ⇒ `g_cal_valid = 0` and **`return true`** with default thresholds. `apds_init()` must not fail because calibration was noisy. The three threshold *writes* at the end (`PIHT`/`GPENTH`/`GEXTH`) are strict — `false` there means the sensor is misconfigured.
- `apds_readGesture()` — any mid-gesture I2C fault returns `GESTURE_NONE` without decoding partial accumulators (a direction from truncated data is untrustworthy).
- `apds_available()` on `GST_GFOV` → `recover_fifo_overflow()`, which increments `g_reinit_count` and calls `sensor_reinit()`; at `RETRY_LIMIT` it stops reinitializing and reports `APDS_ERR_SENSOR_HANG`. Counter is cleared only by a clean `GSTATUS` read.
- `sensor_reinit()` = `configure_registers()` + replay of `g_cal_*` when `g_cal_valid` — any new calibrated register must be added there too, or it silently reverts to the compile-time default after a GFOV.

## Calibration threshold split (`apds9960.c:236-279`)

`median > 100` ⇒ gesture-mode branch: `GPENTH = median/4` (*below* background, engine always armed), sanity-reject if `prox_th > 100`.
`median <= 100` ⇒ proximity-mode branch: `GPENTH = median + APDS_CAL_SIGMA_COEFF*sigma` (*above* background), reject if `> 100` **or** `< APDS_PROX_THRESHOLD`.
Both reject paths restore compile-time defaults and set `g_cal_valid = 0`. `GEXTH` is always 60% of entry, floored at 1. `sigma` uses a `/4` prescale in the variance sum then `*4` after `isqrt` to stay in `uint16_t`.

## Gesture loop (v1.3.0)

`apds_readGesture()` blocks until `GVALID` clears, bounded by `APDS_GESTURE_TIMEOUT_MS` (default 300, range 1..1000 enforced by `#error`). Timing comes from `APDS_OWN_SYSTICK` (default 1): the driver reconfigures SysTick as a free-running up-counter (HCLK/8, CNT=0, CMP=max, no interrupt) for the duration of the call and never calls `Delay_*` inside the loop (`poll_delay()` busy-waits on CNT; `timebase_expired()` compares CNT to `ticks_per_ms * deadline`). With `APDS_OWN_SYSTICK=0` the loop is bounded by an iteration budget instead (each iteration ≥ 1 ms). Historical context: the old code built its deadline on `SysTick->CNT` *alongside* `Delay_Ms(1)` — which zeroes CNT and stops the counter — so the timeout never fired at all.

Direction (v1.3.0): per-packet `ud_ratio=(U-D)*100/(U+D)`, `lr_ratio=(L-R)*100/(L+R)`; `delta = avg(last EDGE_SAMPLES=2 valid packets) − avg(first 2)` per axis (`g_*_first_sum` + `g_*_ring` ring buffer). Equivalent to the old telescoped delta sum, but immune to a single noisy boundary frame. Decode needs `g_packet_count >= EDGE_MIN_PACKETS` (≥ 4 even if config says less) and `max(|delta|) >= APDS_GESTURE_SENSITIVITY`. Sensitivity values tuned for the old metric may need re-tuning (working range 5..40).

## Stack budget (v1.3.0)

`process_fifo_batch()` reads FIFO in chunks of `FIFO_CHUNK_PACKETS=8` (32 B stack buffer): `apds_readGesture` frame is 96 B, full call chain to `i2c_wait_star1_flag` ≈ 148 B against the 256 B stack reservation — fits. Do not enlarge the chunk buffer, do not add large automatics on these paths, and do not raise `APDS_CAL_SAMPLES` above 32 (guarded by `#error`). `calibrate_proximity()` frame is 60 B (64 B with debug prints).

## EXTI ownership (`int_config.c`)

All EXTI0..7 share one vector on CH32V003. `APDS_PROVIDE_EXTI_ISR=1` (default) makes this file define a **strong** `EXTI7_0_IRQHandler` — required because the NoneOS-SDK weak fallback handler is an infinite loop, so a weak definition here can lose to it and hang the MCU on the first interrupt. Applications that own the vector must build with `-DAPDS_PROVIDE_EXTI_ISR=0` and call `apds_handle_exti()` + `apds_clear_exti()`; the lighter hook is the weak `apds_exti_callback()`. `apds_handle_exti()` intentionally does not clear the pending bit.
`APDS_PROVIDE_EXTI_ISR` is defined in *both* `apds9960_config.h` and `int_config.h` (same `#ifndef` default) — a `-D` override must therefore be build-wide, not per-file.
