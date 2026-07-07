# AGENTS.md

## Project

APDS9960 gesture driver for CH32V003 microcontroller (WCH, RISC-V). Detects 4 gestures: LEFT, RIGHT, UP, DOWN. No OS, bare-metal PlatformIO project.

## Build & Upload

```bash
pio run            # build
pio run -t upload  # flash via WCH-Link
```

No test framework configured. Manual testing via UART output (PD5, 115200 baud).

## Platform Constraints

- **RAM:** 2048 bytes, **Flash:** 16384 bytes
- **No float** — all math is integer-only
- **No dynamic allocation** — no malloc/free
- **Framework:** NoneOS-SDK (NOT Arduino, NOT FreeRTOS)
- **Toolchain:** ch32v PlatformIO platform, `ch32v003f4p6_evt_r0` board

## Key Files

| File | Purpose |
|------|---------|
| `src/apds9960.h` | Public API + all configurable #defines |
| `src/apds9960.c` | Driver implementation (~700 lines) |
| `src/apds9960_regs.h` | APDS9960 register map |
| `src/int_config.h` / `src/int_config.c` | EXTI interrupt setup (INT→PC3) |
| `examples/basic/main.c` | Example usage (polling + interrupt modes) |
| `platformio.ini` | Build config, library deps (`I2C-CH32V003`) |
| `library.json` | PlatformIO library manifest (for `lib_deps`) |
| **Library:** `I2C-CH32V003` | I2C driver (via `lib_deps` in `platformio.ini`) |

## Architecture Notes

- **Gesture detection is hardware-driven.** APDS9960 fills FIFO internally. Driver reads FIFO, computes ratios, determines direction.
- **Calibration** runs automatically in `apds_init()` → `calibrate_proximity()`. Takes 32 PDATA samples, computes median + sigma, sets GPENTH/GEXTH/PIHT. Can be disabled with `APDS_ENABLE_CALIBRATION=0`.
- **Gesture vs Proximity mode thresholds:** Gesture mode (PDATA > 100) uses `GPENTH = median/4` (below background). Proximity mode uses `GPENTH = median + 3*sigma` (above background).
- **`sensor_reinit()`** restores calibrated thresholds after FIFO overflow or I2C errors. Uses static `g_cal_*` variables.
- **Cooldown**: removed. `apds_readGesture()` blocks on a real `SysTick`-based deadline (`APDS_GESTURE_TIMEOUT_MS`) until GVALID actually clears, instead of a fixed iteration cap — this alone prevents a single physical swipe from being split into two reported gestures, so no artificial post-gesture delay is needed in `main.c` anymore.
- **Power states:** `apds_sleep()` keeps PON (~1 µA), `apds_shutdown()` clears PON (<1 µA, IR LED off). `apds_wakeup()` handles both correctly (PON first, 1ms delay, then enable everything).
- **Interrupt mode:** `APDS_INT_MODE=1` enables gesture interrupt on PC3 (EXTI3 falling-edge). ISR sets `g_apds_int_flag`, main loop uses `__WFI()`. No NVIC disable/enable dance around gesture handling — the ISR is trivial (flag + clear pending) and safe to leave enabled during I2C polling.
- **I2C retries:** `RETRY_LIMIT=6` in `apds9960.c` gates two things only: (1) how many times `apds_init()` retries the *entire* `configure_registers()` sequence, and (2) the ceiling on consecutive `sensor_reinit()` calls in `apds_available()`. It is NOT a per-call retry inside `rd()`/`wr()`/`rdBlock()` — a single failed I2C register access is not automatically retried at that level.
- **FIFO reads are batched.** `process_fifo_batch()` reads all `GFLVL` packets in one `rdBlock()` transaction (up to 32×4=128 bytes) instead of one I2C transaction per packet — keeps up with the sensor's fill rate and reduces GFOV risk.
- **I2C runs at 400 kHz** (Fast-mode, set in `main.c`). The `I2C-CH32V003` library supports both 100 kHz/400 kHz.

## Gotchas

- PDATA in proximity-only mode saturates (~247) due to LED_BOOST=300% + PGAIN=8x. Gesture mode gives realistic values (~188-191). Calibration works correctly only in gesture mode.
- `SysTick->CNT` counts DOWN on CH32V003. Use `start_time - SysTick->CNT` (wraps correctly for 32-bit unsigned).
- `APDS9960_DEBUG` macro gates all printf output. Comment it out to enable debug prints (costs ~2400 bytes flash).
- Interrupt mode requires `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)` before `GPIO_EXTILineConfig()`. Without it, EXTI mapping is silently ignored.

