# Task Completion

No linter, formatter, type checker or unit test runner exists. `-Wall -Wunused -Wuninitialized` is the only static gate, so a clean build is the real check.

## Required

```powershell
pio run 2>&1 | Select-String -Pattern "RAM:|Flash:|error|warning|SUCCESS|FAILED"
```

Must be `[SUCCESS]` with **zero warnings** — warnings are not tolerated in this codebase, and on rv32ec a new one usually means a real bug. Compare RAM/Flash against `mem:tech_stack` baselines: default is 504 B RAM / 11684 B flash of 2048 B / 16384 B. An unexplained jump (especially flash approaching ~16 KB or a new `printf`/64-bit/float helper being pulled in) blocks the change.

## Config build matrix (when touching `apds9960_config.h`, `#if`-guarded code, or anything in `apds9960.c`/`int_config.c`)

Conditional compilation means the default build exercises only one path. Verify each independently (see `mem:suggested_commands` for the `PLATFORMIO_BUILD_FLAGS` form and the mandatory cleanup):

- `-DAPDS_INT_MODE=0` — polling path; `int_config.*` and `g_apds_int_flag` drop out of the build.
- `-DAPDS_ENABLE_CALIBRATION=0` — `isqrt`/`sort_u8`/`calibrate_proximity` drop out; `apds_recalibrate()` must still compile (returns `APDS_ERR_UNSUPPORTED`).
- `-DAPDS9960_DEBUG` — every `#ifdef`-guarded `printf` gets compiled; catches format/variable mistakes invisible in release builds and confirms flash still fits.
- `-DAPDS_PROVIDE_EXTI_ISR=0` — the strong `EXTI7_0_IRQHandler` is removed; only meaningful together with `APDS_INT_MODE=1`.
- `-DAPDS_OWN_SYSTICK=0` — iteration-budget timeout path replaces the SysTick timebase.

CI runs this whole matrix with `-Wextra -Werror` via `PLATFORMIO_BUILD_SRC_FLAGS` (project sources only — SDK/library warnings don't fail the build).

Finish with a plain `pio run` so `.pio/build/` holds the default firmware.

## Docs

Behavior changes are not done until the bilingual docs are synced: both `README.md`/`README_RU.md` and, if integration/config surface changed, both `INTEGRATION.md`/`INTEGRATION_RU.md`; plus `AGENTS.md` when an architecture note or gotcha becomes wrong. New tunable ⇒ default + `#error` guard + both README config tables.

## Hardware verification (cannot be done from the agent)

Flash and watch UART for the four gestures:

```powershell
pio run -t upload; pio device monitor -b 115200
```

Report which checks were build-only and state explicitly that on-device gesture behavior (thresholds, calibration quality, gesture splitting, EXTI wiring) was not verified.
