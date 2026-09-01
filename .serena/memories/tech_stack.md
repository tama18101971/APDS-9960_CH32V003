# Tech Stack

- Language: **C only** (`-std=gnu99`). No C++ TU in the project. Headers use plain `#ifndef` guards, no `extern "C"`.
- MCU: CH32V003F4P6 (WCH, RISC-V RV32EC). Board `ch32v003f4p6_evt_r0`, 48 MHz HSI.
- Framework: `noneos-sdk` (WCH NoneOS-SDK, package `framework-wch-noneos-sdk`). NOT Arduino, NOT FreeRTOS. SDK headers used directly: `ch32v00x.h`, `debug.h` (`Delay_Init`/`Delay_Ms`, `USART_Printf_Init`, `printf`).
- Build system: PlatformIO Core 6.1.19, platform `ch32v`, compiler `riscv-wch-elf-gcc`.
- Effective compile flags: `-march=rv32ecxw -mabi=ilp32e -Os -g -Wall -Wunused -Wuninitialized -ffunction-sections -fdata-sections`. `rv32ec` = **no hardware mul/div and no M-extension**, and `ilp32e` = 16 registers → keep 32-bit divides/`%` rare, they call libgcc helpers and cost flash.
- Standard headers actually used: `stdint.h`, `stdbool.h`, `stdio.h` (printf under `APDS9960_DEBUG`). No `string.h`, no `math.h` — integer `isqrt()` is hand-rolled in `apds9960.c`.

## Dependency

Single lib dep: `I2C-CH32V003` v7.0.1 (github.com/tama18101971/I2C-CH32V003), **pinned `#v7.0.1`** in both `platformio.ini` `lib_deps` and `library.json` `dependencies` → resolves into `.pio/libdeps/ch32v003f4p6_evt_r0/I2C-CH32V003/src/i2c.h`.

API used by the driver: `i2c_init(uint32_t bound)`, `i2c_read_register`, `i2c_write_register`, `i2c_read_buffer`. Status codes: `I2C_OK 0`, `I2C_NACK 1`, `I2C_ERR_TIMEOUT 2`, `I2C_ERR_CLK 3`, `I2C_ERR_BERR 4`, `I2C_ERR_ARLO 5` — these are the raw values returned by `apds_getLastI2CStatus()` (the pin is part of this library's public contract, hence the version pin).
The lib also exposes `I2C_LITE`, `I2C_TIMEOUT_MS`, `I2C_LEGACY_STATUS`, buffer16/raw APIs; the driver uses none of them. Its internal delays are NOP-loops — it never touches SysTick.

## Measured footprint (v1.3.0, `pio run`, `-Wextra -Werror` clean)

| Variant | RAM | Flash |
|---|---|---|
| default (`APDS_INT_MODE=1`, calibration on) | 512 B (25.0%) | 11852 B (72.3%) |
| `-DAPDS_INT_MODE=0` | 500 B | 11268 B |
| `-DAPDS_ENABLE_CALIBRATION=0` | 508 B | 11204 B |
| `-DAPDS9960_DEBUG` | 512 B | 12312 B |
| `-DAPDS_PROVIDE_EXTI_ISR=0` | 512 B | 11772 B |
| `-DAPDS_OWN_SYSTICK=0` | 508 B | 11776 B |

CI (`.github/workflows/ci.yml`) enforces this matrix plus size ceilings (RAM ≤ 600 B, flash ≤ 13000 B). Stack: `apds_readGesture` frame 96 B, full chain ~148 B vs 256 B reserved. Driver `.text` ≈ 2.4 KB.
