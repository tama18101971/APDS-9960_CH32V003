# AGENTS.md

## Project

APDS9960 gesture driver for CH32V003 microcontroller (WCH, RISC-V). Detects 4 gestures: LEFT, RIGHT, UP, DOWN. No OS, bare-metal PlatformIO project.

## Build & Upload

```bash
pio run            # build
pio run -t upload  # flash via WCH-Link
```

CI (`.github/workflows/ci.yml`) builds five config variants with
`-Wextra -Werror` on project sources (`PLATFORMIO_BUILD_SRC_FLAGS`, so SDK
and library sources are not affected) plus a size-ceiling guard (RAM ≤ 600 B,
flash ≤ 13000 B for the default build).

No test framework configured. Manual testing via UART output (PD5, 115200 baud).

## Platform Constraints

- **RAM:** 2048 bytes, **Flash:** 16384 bytes
- **No float** — all math is integer-only
- **No dynamic allocation** — no malloc/free
- **Framework:** NoneOS-SDK (NOT Arduino, NOT FreeRTOS)
- **Toolchain:** ch32v PlatformIO platform, `ch32v003f4p6_evt_r0` board
- **Stack:** linker script reserves 256 bytes; peak call chain
  `apds_readGesture` → i2c ≈ 148 bytes — keep new automatics small.

## Key Files

| File | Purpose |
|------|---------|
| `src/apds9960.h` | Public API + `APDS_ERR_*` codes |
| `src/apds9960_config.h` | ALL tunables with `#ifndef` defaults + `#error` range validation |
| `src/apds9960.c` | Driver implementation |
| `src/apds9960_regs.h` | APDS9960 register map |
| `src/int_config.h` / `src/int_config.c` | EXTI interrupt setup (INT→PC3) |
| `examples/basic/main.c` | Example usage (polling + interrupt modes) |
| `platformio.ini` | Build config, library deps (`I2C-CH32V003`, pinned `#v7.0.1`) |
| `library.json` | PlatformIO library manifest (v1.3.0, excludes `examples/`) |
| `LICENSE` | MIT license |
| **Library:** `I2C-CH32V003` | I2C driver (via `lib_deps`, pinned `#v7.0.1`) |

## Architecture Notes

- **Gesture detection is hardware-driven.** APDS9960 fills FIFO internally. Driver reads FIFO, computes ratios, determines direction.
- **Calibration** runs automatically in `apds_init()` → `calibrate_proximity()`. Takes 32 PDATA samples, computes median + sigma, sets GPENTH/GEXTH. Can be disabled with `APDS_ENABLE_CALIBRATION=0` (then `apds_recalibrate()` returns `APDS_ERR_UNSUPPORTED`).
- **Gesture vs Proximity mode thresholds:** Gesture mode (PDATA > 100) uses `GPENTH = median/4` (below background). Proximity mode uses `GPENTH = median + 3*sigma` (above background).
- **`sensor_reinit()`** restores calibrated thresholds after FIFO overflow or I2C errors. Uses static `g_cal_*` variables (`g_cal_piht` removed — PIHT is not written since PIEN is never enabled).
- **Gesture direction** = difference of averaged edges: `avg(last 2 valid packets) − avg(first 2 valid packets)` per axis. Equivalent to the old telescoped ratio sum, but immune to a single noisy boundary frame. `EDGE_MIN_PACKETS` raises the effective minimum to 4 packets when `APDS_FIFO_MIN_PACKETS < 4`.
- **Gesture timeout:** with the default `APDS_OWN_SYSTICK=1`, the driver reconfigures SysTick as a free-running counter (HCLK/8) for the duration of `apds_readGesture()` and does not call `Delay_*` inside the loop. With `APDS_OWN_SYSTICK=0` the loop is bounded by an iteration budget (each iteration ≥ 1 ms). The historical claim "SysTick counts DOWN" was wrong — NoneOS-SDK `Delay_*` zero CNT and count UP to CMP.
- **Cooldown**: removed. `apds_readGesture()` waits until GVALID actually clears, bounded by `APDS_GESTURE_TIMEOUT_MS`, so no artificial post-gesture delay is needed in `examples/basic/main.c`.
- **Power states:** `apds_sleep()` keeps PON (~1 µA), `apds_shutdown()` clears PON (<1 µA, IR LED off). `apds_wakeup()` handles both correctly (PON first, 1ms delay, then enable everything) and resets gesture state (`gesture_reset()` + `g_reinit_count = 0`).
- **Interrupt mode:** `APDS_INT_MODE=1` enables gesture interrupt on PC3 (EXTI3 falling-edge). ISR sets `g_apds_int_flag`, main loop uses `__WFI()`. `GINT` in the sensor is cleared by fully draining the FIFO (done by `apds_readGesture()`), NOT by reading GSTATUS — `apds_clearInterrupt()` is therefore optional in the hot path.
- **I2C retries:** `RETRY_LIMIT=6` in `apds9960.c` gates two things only: (1) how many times `apds_init()` retries the *entire* `configure_registers()` sequence, and (2) the ceiling on consecutive `sensor_reinit()` calls in `apds_available()`. It is NOT a per-call retry inside `rd()`/`wr()`/`rdBlock()` — a single failed I2C register access is not automatically retried at that level.
- **FIFO reads are chunked.** `process_fifo_batch()` reads `GFLVL` packets in chunks of 8 (one `rdBlock()` per chunk, 32 bytes on stack) — batching keeps up with the sensor's fill rate, while the stack frame stays ~96 B (a full-FIFO 128-byte buffer used to overflow the 256-byte stack together with the call chain).
- **I2C runs at 400 kHz** (Fast-mode, set in `examples/basic/main.c`). The `I2C-CH32V003` library supports both 100 kHz/400 kHz.
- **I2C error diagnostics (I2C-CH32V003 v7.0.x):** wrappers `rd()`/`wr()`/`rdBlock()` store the raw status of the last FAILED transaction (`I2C_NACK`, `I2C_ERR_TIMEOUT`, `I2C_ERR_BERR`, `I2C_ERR_ARLO`) in static `g_last_i2c_status`, exposed via `apds_getLastI2CStatus()`. Written only on failures; success never resets it. When `APDS_ERR_FIFO_OVERFLOW` is reported, the raw I2C status may refer to the recovery sequence (`sensor_reinit()`), not to the caller-visible operation.
- **I2C fault tolerance policy:** transient I2C faults during calibration *sampling* abort collection early and fall back to default thresholds instead of failing `apds_init()` (threshold writes remain strict). `apds_readGesture()` returns `GESTURE_NONE` immediately on an I2C fault mid-gesture rather than decoding partial data. Void interrupt-API functions (`apds_enableInterrupt/disableInterrupt/clearInterrupt`) record `APDS_ERR_I2C` on failure and do not touch `g_last_error` on success.
- **LED boost** `APDS_LED_BOOST` (CONFIG2, default 3 = 300%) scales the LED current set by `APDS_LED_CURRENT`. It is the primary cause of PDATA saturation at close range; lower it before touching gains.

## Gotchas

- PDATA in proximity-only mode saturates (~247) due to LED_BOOST=300% + PGAIN=8x. Gesture mode gives realistic values (~188-191). Calibration works correctly only in gesture mode.
- `SysTick->CNT` counts UP on CH32V003, and NoneOS-SDK `Delay_Ms()`/`Delay_Us()` reset CNT to 0, set CMP, then STOP the counter after each call — SysTick is NOT a free-running timebase while `Delay_*` is in use. Never build deadlines on `SysTick->CNT` alongside `Delay_*`.
- `APDS9960_DEBUG` macro gates all printf output. Comment it out to enable debug prints (costs ~2400 bytes flash).
- Interrupt mode requires `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)` before `GPIO_EXTILineConfig()`. Without it, EXTI mapping is silently ignored.
