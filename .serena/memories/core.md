# Core

APDS9960 gesture driver (LEFT/RIGHT/UP/DOWN) for CH32V003, bare-metal PlatformIO library.
Repo doubles as **library + demo firmware**: `origin` = github.com/tama18101971/APDS-9960_CH32V003, released via git tags (`v1.2.3` = current).

`AGENTS.md` is auto-loaded into every session (build/upload commands, platform limits, architecture notes, gotchas). Do not duplicate it in memories; only correct/extend it.

## Source map

| Path | Role |
|---|---|
| `src/apds9960.h` | public API, error codes `APDS_ERR_*`, `gesture_t`; includes `apds9960_config.h` |
| `src/apds9960_config.h` | ALL tunables + `#error` range validation |
| `src/apds9960.c` | whole driver, ~840 lines, single translation unit |
| `src/apds9960_regs.h` | register addresses + bit masks (`REG_*`, `EN_*`, `ST_*`, `GST_*`, `GC4_*`, shift/mask macros) |
| `src/int_config.{h,c}` | EXTI3 (INT→PC3) setup, `g_apds_int_flag`, `EXTI7_0_IRQHandler` |
| `examples/basic/main.c` | demo, compiled into the default firmware (see below) |
| `include/`, `lib/`, `test/` | empty PlatformIO placeholders (README only) |

## Project-wide invariants

- `platformio.ini` sets `src_dir = .` and `build_src_filter = +<src/> +<examples/basic/>` → **the example `main()` is part of the default build**. When packaging as a library for another project, `examples/` must be excluded or `main()` collides (INTEGRATION.md §1, §5.1).
- Driver params are compile-time only and `apds9960.c` is a separate TU: `#define` in application code before `#include "apds9960.h"` has NO effect. The only supported override path is PlatformIO `build_flags` / `-D`.
- `apds9960_config.h` is included by both app and driver; every option has an `#ifndef` default plus an `#error` range guard, so an invalid `-D` breaks the build instead of misconfiguring the sensor.
- All driver state is file-scope `static` in `apds9960.c` (`g_*`); the driver is single-instance and not reentrant. Statics consumed by `calibrate_proximity()` (`g_cal_*`) are declared above it deliberately.
- No float, no malloc, no OS. RAM budget 2048 B / Flash 16384 B is the hard constraint on every change.

Driver-level invariants not covered by `AGENTS.md` (error/status model, calibration fallbacks, EXTI ownership, stack hot spot): `mem:driver_internals`
Toolchain, ISA flags, dependency layout, measured footprint per build variant: `mem:tech_stack`
PlatformIO/PowerShell command forms, incl. probing alternative configs without editing `platformio.ini`: `mem:suggested_commands`
Naming, comment language, bilingual docs and release/version-bump ritual: `mem:conventions`
What to build/verify before declaring a change done (incl. the config-variant build matrix): `mem:task_completion`
