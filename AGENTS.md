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
| `src/i2c.c` / `src/i2c.h` | I2C driver for CH32V003 |
| `src/main.c` | Entry point, usage example |

## Architecture Notes

- **Gesture detection is hardware-driven.** APDS9960 fills FIFO internally. Driver reads FIFO, computes ratios, determines direction.
- **Calibration** runs automatically in `apds_init()` → `calibrate_proximity()`. Takes 32 PDATA samples, computes median + sigma, sets GPENTH/GEXTH/PIHT. Can be disabled with `APDS_ENABLE_CALIBRATION=0`.
- **Gesture vs Proximity mode thresholds:** Gesture mode (PDATA > 100) uses `GPENTH = median/4` (below background). Proximity mode uses `GPENTH = median + 3*sigma` (above background).
- **`sensor_reinit()`** restores calibrated thresholds after FIFO overflow or I2C errors. Uses static `g_cal_*` variables.
- **Cooldown** in main loop uses `SysTick->CNT` (32-bit down counter, 48 MHz) — non-blocking. Do NOT use `Delay_Ms()` for cooldown.
- **Power states:** `apds_sleep()` keeps PON (~1 µA), `apds_shutdown()` clears PON (<1 µA, IR LED off). `apds_wakeup()` handles both correctly (PON first, 1ms delay, then enable everything).
- **Interrupt mode:** `APDS_INT_MODE=1` enables gesture interrupt on PC3 (EXTI3 falling-edge). ISR sets `g_apds_int_flag`, main loop uses `__WFI()`. Cooldown disables NVIC, delays, drains FIFO, re-enables NVIC.
- **I2C retries:** `RETRY_LIMIT=6` in `apds9960.h`. All I2C writes go through `wr()` which retries on failure.

## Gotchas

- PDATA in proximity-only mode saturates (~247) due to LED_BOOST=300% + PGAIN=8x. Gesture mode gives realistic values (~188-191). Calibration works correctly only in gesture mode.
- `SysTick->CNT` counts DOWN on CH32V003. Use `start_time - SysTick->CNT` (wraps correctly for 32-bit unsigned).
- `APDS9960_DEBUG` macro gates all printf output. Comment it out to enable debug prints (costs ~2400 bytes flash).
- Interrupt mode requires `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)` before `GPIO_EXTILineConfig()`. Without it, EXTI mapping is silently ignored.

