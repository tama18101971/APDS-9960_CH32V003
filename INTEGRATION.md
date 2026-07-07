# Integrating the APDS9960 Driver as a Library in Other Projects

🇬🇧 English | [🇷🇺 Русский](INTEGRATION_RU.md)

This document describes how to use the APDS9960 gesture driver (`apds9960.*`,
`apds9960_regs.h`, `int_config.*`) in **other** PlatformIO projects on CH32V003,
without copying `main.c` (it is only a demo example for this repository).

The I2C driver is included as an external dependency (`I2C-CH32V003`) via `lib_deps`.

## 1. Library Files

| File | Required? | Purpose |
|------|-----------|---------|
| `apds9960_regs.h` | Always | APDS9960 register map |
| `apds9960.h` / `apds9960.c` | Always | Gesture driver itself |
| `int_config.h` / `int_config.c` | Only if `APDS_INT_MODE=1` | EXTI3 (PC3) for interrupt mode |

The I2C driver (`i2c.h` / `i2c.c`) is an **external dependency**
[`I2C-CH32V003`](https://github.com/tama18101971/I2C-CH32V003.git).
Connected via `lib_deps` in `platformio.ini`, no need to copy it into your project.

If the new project only works in polling mode (`APDS_INT_MODE=0`),
`int_config.h` / `int_config.c` can be omitted entirely.

## 2. Target Project Requirements

The driver is written for a specific platform and is NOT MCU-agnostic:

- PlatformIO platform `ch32v`, framework `noneos-sdk` (NOT Arduino, NOT FreeRTOS).
- Actual CH32V003 chip (or register-compatible with I2C1/EXTI/SysTick).
- The target project's `platformio.ini` must contain at minimum:

```ini
[env:my_board]
platform = ch32v
board = ch32v003f4p6_evt_r0   ; or another CH32V003 board
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/I2C-CH32V003.git
```

For Method C, also add the library itself (see section 5.2).

- Free pins: **PC1 (SDA)**, **PC2 (SCL)**, and (only for interrupt mode)
  **PC3 (INT)**. If these pins are occupied by other peripherals in the target
  project, you need to either free them or override the interrupt pin parameters
  (`APDS_INT_PORT`, `APDS_INT_PIN`, `APDS_INT_LINE`, `APDS_INT_PORT_SOURCE`,
  `APDS_INT_PIN_SOURCE`) via `build_flags` (see section 8).
- I2C1 bus is free (or already used only by devices compatible by address —
  APDS9960 sits on `0x39`).

## 3. Method A — Quick Copy (for one-time integration)

The simplest option, no separate git repository needed.

1. Create a `lib/APDS9960/` folder in the target project (PlatformIO automatically
   includes everything from `lib/*` into the build, without mixing with `src/`):

   ```
   your_project/
     lib/
       APDS9960/
         apds9960.h
         apds9960.c
         apds9960_regs.h
         int_config.h      (optional)
         int_config.c      (optional)
     src/
       main.c              (your own main() — NOT from this repository)
   ```

2. Copy the files from section 1 (without `main.c`).
3. Add the I2C dependency in `platformio.ini`:

   ```ini
   lib_deps =
       https://github.com/tama18101971/I2C-CH32V003.git
   ```

4. In your `src/main.c`, include as usual:

   ```c
   #include "i2c.h"
   #include "apds9960.h"
   #if APDS_INT_MODE == 1
   #include "int_config.h"
   #endif
   ```

5. `pio run` — PlatformIO will pick up `lib/APDS9960/` and `I2C-CH32V003` automatically.

**Downside of Method A:** when bugs are fixed in this repository, changes must be
manually copied to every project that has a local copy.

## 4. Method B — Shared Local Folder (multiple local projects, one developer)

If you have multiple projects on one machine and don't want to duplicate files —
keep the library in one place and connect it via `lib_extra_dirs`.

1. Place the driver files (without `main.c`) in a separate folder, e.g.:
   `C:\Projects\shared_libs\APDS9960_CH32V003\` (flat, or inside a `src/` subfolder —
   PlatformIO searches for headers/sources recursively).

2. In `platformio.ini` of **each** project that needs the driver:

   ```ini
   [env:my_board]
   platform = ch32v
   board = ch32v003f4p6_evt_r0
   framework = noneos-sdk
   lib_extra_dirs = C:\Projects\shared_libs
   ```

3. `pio run` will include `APDS9960_CH32V003` as a regular library from
   `lib_extra_dirs`.

**Pro:** single source of truth on disk, edit in one place — picked up everywhere
on next build. **Con:** works only on this machine, not suitable for CI/other
developers without folder synchronization.

## 5. Method C — Separate Git Repository + `lib_deps` (recommended for multiple projects)

The most correct approach when the driver is used in multiple projects and/or
by multiple people/machines: extract the driver into its own git repository
(or git submodule) and connect via `lib_deps` — PlatformIO will download and
cache the library automatically during build.

### 5.1. Preparing the Library Repository (one-time)

This repository already has a ready-made structure for publishing as a library:

```
APDS-9960_CH32V003/            (git repository root)
  library.json
  src/
    apds9960.h
    apds9960.c
    apds9960_regs.h
    int_config.h
    int_config.c
  examples/
    basic/
      main.c                   (demo example, NOT compiled as part of the library)
```

See `library.json` in the repository root. It already includes the `I2C-CH32V003`
dependency and excludes `examples/` from library compilation.

If you are creating your **own** library repository — copy the files from `src/`
and create an analogous `library.json`.

### 5.2. Connecting in the Target Project

```ini
[env:my_board]
platform = ch32v
board = ch32v003f4p6_evt_r0
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git
```

Or with version pinning (tag/commit/branch):

```ini
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.0.0
```

If you are creating your **own** fork/repository of the library — replace the
path with your own: `<your_account>/APDS9960_CH32V003.git`.

`I2C-CH32V003` is installed **automatically** — it is declared in the `dependencies`
field of `library.json`, so there is no need to list it in `lib_deps`.

`pio run` will clone the repository into `.pio/libdeps/<env>/` on first build.

**Pro:** single source of truth, versioning, works on any machine and in CI.
**Con:** requires initial setup (git repository + `library.json`).

## 6. Minimal Usage Example (after connecting by any method)

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#if APDS_INT_MODE == 1
#include "int_config.h"
#endif

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    i2c_init(400000);              /* Fast-mode I2C, sensor and I2C-CH32V003 both support it */

    if (!apds_init()) {
        printf("APDS9960 not responding!\r\n");
        while (1) {}
    }

#if APDS_INT_MODE == 1
    apds_exti_init();
    apds_enableInterrupt();
#endif

    while (1) {
#if APDS_INT_MODE == 1
        while (g_apds_int_flag == 0) { __WFI(); }
        g_apds_int_flag = 0;
        apds_clearInterrupt();
#endif
        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }
        }
    }
}
```

Full working example (both modes, with ISR example) — see `examples/basic/main.c`
in this repository.

## 7. Pin Connections (same regardless of integration method)

| CH32V003 | APDS9960 | Function |
|----------|----------|----------|
| PC1 | SDA | I2C data |
| PC2 | SCL | I2C clock |
| PC3 | INT | Gesture interrupt (active-low), only for `APDS_INT_MODE=1` |
| PD5 | — | UART TX (diagnostics, optional) |

Sensor I2C address: `0x39`.

## 8. Project-Specific Configuration

All parameters are overridden via `#define` **before** `#include "apds9960.h"`,
or via `build_flags` in `platformio.ini` (e.g. `-DAPDS_INT_MODE=0`):

Interrupt pin parameters from `int_config.h` are also overridden via `build_flags`:

```ini
build_flags =
    -DAPDS_INT_PORT=GPIOC
    -DAPDS_INT_PIN=GPIO_Pin_3
    -DAPDS_INT_LINE=EXTI_Line3
    -DAPDS_INT_PORT_SOURCE=GPIO_PortSourceGPIOC
    -DAPDS_INT_PIN_SOURCE=GPIO_PinSource3
```

```c
#define APDS_GAIN               3     /* proximity gain: 0=1x..3=8x */
#define APDS_LED_CURRENT         0     /* LED current: 0=100mA..3=12.5mA */
#define APDS_GGAIN               3     /* gesture gain: 0=1x..3=8x */
#define APDS_GLDRIVE             0     /* gesture LED current: 0=100mA..3=12.5mA */
#define APDS_PROX_THRESHOLD      50    /* gesture mode entry threshold (before calibration) */
#define APDS_GESTURE_EXIT_TH     30    /* exit threshold (before calibration) */
#define APDS_GESTURE_TIMEOUT_MS  300   /* max gesture duration, ms */
#define APDS_GWTIME              1     /* gesture sample wait time */
#define APDS_ENABLE_CALIBRATION  1     /* auto-calibrate thresholds in apds_init() */
#define APDS_INT_MODE            1     /* 0=polling, 1=interrupt (PC3/EXTI3) */
/* #define APDS9960_DEBUG */          /* enable printf diagnostics (~2.4 KB Flash) */

#include "apds9960.h"
```

Full list and description — in `apds9960.h` comments.

## 9. Resource Budget (consider for new projects)

On the CH32V003 chip (RAM 2 KB, Flash 16 KB), the driver itself uses approximately:

- **RAM:** ~20 bytes static + up to ~150 bytes stack at peak (batch FIFO read)
- **Flash:** ~2.0 KB (`apds9960.c` + `int_config.c` only, without I2C library)

If the target project already uses a significant portion of Flash/RAM with other
code — verify the actual budget via `pio run` (output "RAM:" / "Flash:").

## 10. Compatibility Checklist Before Porting

- [ ] Target project uses `platform = ch32v`, `framework = noneos-sdk`
- [ ] PC1/PC2 (and PC3, if interrupt mode) are free
- [ ] I2C1 is not occupied by another device at address `0x39`
- [ ] Enough Flash/RAM available (see section 9)
- [ ] `lib_deps` with `I2C-CH32V003` added to `platformio.ini`

## 11. Common Porting Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| `apds_init()` returns `false` | Incorrect I2C wiring, or PC1/PC2 occupied by other peripherals | Check pins, use `APDS9960_DEBUG` to print ID |
| Gestures not detected | Signal too weak / incorrect calibration | Increase `APDS_GAIN` / `APDS_GGAIN`, check lighting (avoid direct sunlight) |
| Gestures "duplicated" | Old version of `apds9960.c` with artificial `apds_readGesture()` cutoff by iteration count | Ensure the latest driver version from this repository is copied |
| Build can't find `ch32v00x.h` / `debug.h` | Wrong `framework` / `platform` in target project's `platformio.ini` | See section 2 |
| Build can't find `i2c.h` | `lib_deps` with `I2C-CH32V003` not added to `platformio.ini` | Add `https://github.com/tama18101971/I2C-CH32V003.git` to `lib_deps` |
| `main()` conflict when connecting via `lib_deps` on the entire repository | `main.c` accidentally ended up in the library | Do not copy/include `main.c` in the library repository (see sections 1 and 5.1) |

## 12. Back-Syncing Fixes

If you find a bug or improve the algorithm after porting to another project —
back-port the fix to this repository (source of truth), then update copies/
dependencies in other projects using the same method they were connected with
(Method A — manually copy again, Method B — editing the single folder on disk
is automatically picked up, Method C — update tag/commit in `lib_deps`).
