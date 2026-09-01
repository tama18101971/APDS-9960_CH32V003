# Conventions

## Language

- **All code comments and doc-comments are in Russian.** New code must keep this; do not "translate to English" existing comments.
- User-facing docs are bilingual pairs kept in sync: `README.md`/`README_RU.md`, `INTEGRATION.md`/`INTEGRATION_RU.md`. `AGENTS.md` is English-only. Editing one language and not the other is a defect — the release commits touch both.
- Commit messages: English, conventional-commit-ish prefixes (`feat:`, `fix:`, `refactor:`, `docs:`, `release:`), optional scope (`refactor(apds9960):`). A few early commits are Russian; follow the current English style.

## Naming

| Kind | Style | Example |
|---|---|---|
| public API | `apds_` + lowerCamelCase verb | `apds_readGesture`, `apds_getLastI2CStatus` |
| public API (lifecycle) | `apds_` + lowercase | `apds_init`, `apds_sleep`, `apds_wakeup` |
| internal helpers | `static`, short snake_case | `rd`, `wr`, `rdBlock`, `process_fifo_batch`, `sensor_reinit` |
| file-scope state | `g_` + snake_case | `g_last_error`, `g_cal_gpenth`, `g_apds_int_flag` |
| registers / bits | `REG_*`, `EN_*`, `ST_*`, `GST_*`, `GC4_*`, `CTRL_*_SHIFT/_MASK` | `REG_GSTATUS`, `GST_GVALID` |
| tunables | `APDS_*` upper snake | `APDS_GESTURE_SENSITIVITY` |
| driver-local aliases of tunables | bare macro aliasing an `APDS_*` | `#define SENSITIVITY_1 APDS_GESTURE_SENSITIVITY` |

## Style

- 4 spaces, no tabs. Braces on the same line. `if (!x) return false;` single-line early returns are idiomatic here.
- Section banners are used heavily and are part of the house style:
  ```c
  /* ============================================================================
   * ЗАГОЛОВОК РАЗДЕЛА
   * ============================================================================ */
  ```
- Every public function in `apds9960.h` carries a Russian block comment ending in `Возвращает: ...`; non-obvious caveats use `@note`. Mirror the same doc on the definition in `apds9960.c` when behavior is subtle.
- Every new tunable follows the full three-part ritual: `#ifndef`/`#define` default in `apds9960_config.h`, matching `#if ... #error "... must be in range ..."` guard, and documentation in both READMEs' configuration table.
- Debug output is always wrapped in `#ifdef APDS9960_DEBUG` — never leave an unguarded `printf` in driver code.
- Integer-only: no float/double literals or ops anywhere. Avoid new 32-bit `/`/`%` on the hot path (soft-div on rv32ec). Guard accumulators against overflow explicitly (see the `/4` prescale in the variance loop and `if (g_packet_count != UINT8_MAX)`).
- Optional code is compiled out, not runtime-flagged: `#if APDS_ENABLE_CALIBRATION`, `#if APDS_INT_MODE == 1`, `#if APDS_PROVIDE_EXTI_ISR`. Keep symbols out of the build entirely when disabled.
- Errors surface via `bool` return + `g_last_error` (now includes `APDS_ERR_UNSUPPORTED=5`); `void` APIs record `g_last_error` on failure and must not clear it on success. `apds_recalibrate()` returns `false` + `APDS_ERR_UNSUPPORTED` when calibration is compiled out.
- **NEVER round-trip source files through PowerShell `Get-Content`/`Set-Content`** — PS 5.1 reads UTF-8 as ANSI and silently mojibakes every Cyrillic comment (this happened once to `apds9960.c` and required a git checkout + redo). Use the `edit` tool or `[System.IO.File]::ReadAllText/WriteAllText` with explicit UTF8 encoding.

## Release ritual

Version lives only in `library.json` (`version`, currently 1.3.0) — bump it, sync both README pairs and `AGENTS.md`, commit as `release: vX.Y.Z <summary>`, then tag `vX.Y.Z`. Footprint tables in the READMEs quote real `pio run` numbers, so re-measure before editing them. `library.json` now also carries `keywords`/`license`/`repository` and an `export.exclude` list — keep `examples/`, `.pio/`, `.serena/` excluded when publishing.
