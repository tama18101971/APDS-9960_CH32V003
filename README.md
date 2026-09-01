# APDS9960 Gesture Driver for CH32V003

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

🇬🇧 English | [🇷🇺 Русский](README_RU.md)

Compact gesture recognition driver for the APDS9960 proximity/light/color sensor, targeting the CH32V003 microcontroller. Detects four directional gestures: **Left**, **Right**, **Up**, **Down**.

## Features

- Minimal RAM usage (~32 bytes static driver, peak stack ~148 bytes inside `apds_readGesture()`)
- Minimal Flash footprint (~3.3 KB driver only, ~11.4 KB full demo project)
- No dynamic memory allocation (`malloc`/`free`)
- No floating-point math — integer-only arithmetic
- Project-wide configurable parameters via PlatformIO `build_flags`
- Auto-calibration of proximity thresholds
- Auto-recovery on FIFO overflow
- Interrupt-driven mode (APDS9960 INT → EXTI3 on PC3)
- Real gesture timeout (free-running SysTick owned by the driver, or iteration budget with `APDS_OWN_SYSTICK=0`)
- Clone-compatible ID check (supports original + Chinese clones)
- Diagnostic API (error codes, reinit count, raw I2C bus status)
- Platform-agnostic C code (no HAL, no Arduino)

## Hardware

### Supported

| Component | Model |
|-----------|-------|
| Microcontroller | CH32V003 (WCH) |
| Sensor | APDS9960 (including clones) |
| Interface | I2C (100 kHz or 400 kHz Fast-mode) |

### Pin Connections

| CH32V003 | APDS9960 | Function |
|----------|----------|----------|
| PC1 | SDA | I2C Data |
| PC2 | SCL | I2C Clock |
| PC3 | INT | Gesture interrupt (active-low, falling-edge) |
| PD5 | — | UART TX (debug output) |

Sensor I2C address: `0x39`

## File Structure

```
src/
  apds9960.h         — Public API
  apds9960_config.h  — Shared driver parameters and range checks
  apds9960.c         — Driver implementation
  apds9960_regs.h    — Register map and bit definitions
  int_config.h / int_config.c — EXTI interrupt setup (PC3)
examples/
  basic/
    main.c           — Usage example (polling + interrupt modes)
library.json         — PlatformIO library manifest
platformio.ini       — Build config (uses build_src_filter to compile src/ + examples/basic/)
```

## Build

Requires [PlatformIO](https://platformio.org/) with the `ch32v` platform.

```bash
pio run            # build
pio run -t upload  # flash via WCH-Link
```

## Using This Driver in Other Projects

See [`INTEGRATION.md`](INTEGRATION.md) for step-by-step instructions on
reusing this driver (`apds9960.*`, `apds9960_config.h`, `apds9960_regs.h`, `int_config.*`)
in other CH32V003/PlatformIO projects — quick copy, shared local library
folder, or a dedicated git-based PlatformIO library. I2C driver is
included as an external dependency (`I2C-CH32V003`) via `lib_deps`.

## API

```c
#include "apds9960.h"

// Initialize sensor (returns false if not found)
bool apds_init(void);

// Put sensor to sleep (~1 uA, PON stays on)
bool apds_sleep(void);

// Full power-off (<1 uA, IR LED off)
bool apds_shutdown(void);

// Wake sensor from sleep or shutdown
bool apds_wakeup(void);

// Check if gesture data is ready
bool apds_available(void);

// Read detected gesture (blocking, ~10-60 ms)
gesture_t apds_readGesture(void);

// Read proximity value (0-255)
bool apds_readProximity(uint8_t *value);

// Read STATUS register for diagnostics
bool apds_readStatus(uint8_t *value);

// Get last error code (APDS_ERR_*)
uint8_t apds_getLastError(void);

// Get raw status of the last FAILED I2C transaction (I2C-CH32V003 v7.0.x codes)
uint8_t apds_getLastI2CStatus(void);

// Get reinit count (0 = normal, >0 = problems)
uint8_t apds_getReinitCount(void);

// Recalibrate thresholds
bool apds_recalibrate(void);

// Enable gesture interrupt (GIEN=1, INT pin active-low)
void apds_enableInterrupt(void);

// Disable gesture interrupt (GIEN=0)
void apds_disableInterrupt(void);

// Clear interrupt: read GSTATUS → INT pin goes high
void apds_clearInterrupt(void);
```

### I2C Error Diagnostics

When `apds_getLastError()` returns `APDS_ERR_I2C`, `apds_getLastI2CStatus()`
reports the raw bus-level code from the
[I2C-CH32V003 v7.0.x](https://github.com/tama18101971/I2C-CH32V003) driver:

| Value | Symbol | Meaning |
|-------|--------|---------|
| 0 | `I2C_OK` | No failure recorded yet |
| 1 | `I2C_NACK` | No ACK from sensor (wiring / wrong address) |
| 2 | `I2C_ERR_TIMEOUT` | Bus timeout (clock stretch / stuck bus) |
| 3 | `I2C_ERR_CLK` | Invalid clock configuration |
| 4 | `I2C_ERR_BERR` | Bus error (START/STOP fault, auto-recovered by the I2C driver) |
| 5 | `I2C_ERR_ARLO` | Arbitration lost |

Behavior notes:

- The raw status is written **only on failures** and is never reset by a
  subsequent successful transaction.
- `apds_readGesture()` returns `GESTURE_NONE` immediately when an I2C fault
  occurs mid-gesture — partially accumulated data is not decoded. Check
  `apds_getLastError()` afterwards.
- Interrupt API (`apds_enableInterrupt()` / `apds_disableInterrupt()` /
  `apds_clearInterrupt()`) records `APDS_ERR_I2C` on failure; on success the
  previous error code is left untouched.
- Transient I2C faults during calibration sampling do not fail `apds_init()`:
  sampling stops early and default thresholds are used if too few valid
  samples remain. Threshold *writes* remain strict.

### EXTI Interrupt API (`int_config.h`)

```c
#include "int_config.h"

// Init EXTI3 + NVIC for APDS9960 interrupt on PC3
void apds_exti_init(void);

// Enable/disable EXTI7_0 interrupt in NVIC
void apds_exti_enable(void);
void apds_exti_disable(void);

// Check APDS_INT_LINE and set g_apds_int_flag (does NOT clear pending bit)
void apds_handle_exti(void);

// Clear pending bit for APDS_INT_LINE
void apds_clear_exti(void);

// Weak callback — called from default EXTI7_0_IRQHandler, override to add custom EXTI handling
void apds_exti_callback(void);
```

### Custom EXTI Handler

On CH32V003, all EXTI lines 0-7 share a single interrupt vector (`EXTI7_0_IRQHandler`).
By default the library provides a **strong** handler so it overrides the looping
NoneOS-SDK fallback handler. To own the shared EXTI0…7 vector, add
`-DAPDS_PROVIDE_EXTI_ISR=0` to `build_flags` and define the handler yourself:

```c
#include "int_config.h"

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    // Check APDS9960 interrupt (sets g_apds_int_flag, pending bit NOT cleared yet)
    apds_handle_exti();

    // Now check your other EXTI lines — pending bits are still set
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        // ... handle EXTI5 ...
        EXTI_ClearITPendingBit(EXTI_Line5);
    }

    // Clear APDS9960 pending bit last
    apds_clear_exti();
}
```

If you only need to add logic after the default handling, override the weak callback instead:

```c
#include "int_config.h"

void apds_exti_callback(void) {
    // Called after default EXTI7_0_IRQHandler handles APDS9960
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        // ... handle EXTI5 ...
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
}
```

### Gesture Types

```c
typedef enum {
    GESTURE_NONE = 0,   // No gesture detected
    GESTURE_LEFT,       // Swipe left
    GESTURE_RIGHT,      // Swipe right
    GESTURE_UP,         // Swipe up
    GESTURE_DOWN        // Swipe down
} gesture_t;
```

### Error Codes

```c
#define APDS_ERR_NONE           0   // No error
#define APDS_ERR_I2C            1   // I2C error — see apds_getLastI2CStatus()
#define APDS_ERR_FIFO_OVERFLOW  2   // FIFO overflow
#define APDS_ERR_SENSOR_HANG    3   // Sensor not responding (also set when sensor_reinit() inside overflow recovery fails)
#define APDS_ERR_INVALID_ID     4   // Device at 0x39 is not an APDS9960
#define APDS_ERR_UNSUPPORTED    5   // Feature compiled out (e.g. apds_recalibrate() with APDS_ENABLE_CALIBRATION=0)
```

## Usage Example

### Interrupt Mode (recommended)

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#include "int_config.h"

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    uint8_t i2c_st = i2c_init(400000);
    if (i2c_st != I2C_OK) {
        printf("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    if (!apds_init()) {
        printf("Sensor not found! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());
        while (1) {}
    }

    // INT → PC3 (EXTI3, falling-edge)
    apds_exti_init();
    apds_enableInterrupt();

    while (1) {
        // CPU sleeps until ISR sets flag
        while (g_apds_int_flag == 0) { __WFI(); }
        g_apds_int_flag = 0;

        // Note: apds_clearInterrupt() is optional here — apds_available()
        // reads GSTATUS itself, and GINT in the sensor is cleared by
        // draining the FIFO inside apds_readGesture().

        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }

            // Drain the FIFO tail. Bounded: the hardware FIFO is 32 packets
            // deep, so at most 8 passes are needed.
            uint8_t drain = 8;
            while (drain-- && apds_available()) apds_readGesture();
        }
    }
}
```

### Polling Mode

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    uint8_t i2c_st = i2c_init(400000);
    if (i2c_st != I2C_OK) {
        printf("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    if (!apds_init()) {
        printf("Sensor not found! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());
        while (1) {}
    }

    while (1) {
        if (apds_available()) {
            gesture_t g = apds_readGesture();
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }

            if (g != GESTURE_NONE) {
                uint8_t drain = 8;
                while (drain-- && apds_available()) apds_readGesture();
            }
        }
    }
}
```

## Algorithm

### Overview

The APDS9960 contains four directional photodiodes (Up, Down, Left, Right) and an IR LED. When an object moves over the sensor, the photodiode readings change. The driver reads these values from the hardware FIFO buffer and computes the gesture direction.

### Data Flow

```
IR LED reflects off hand
        |
        v
APDS9960 photodiodes (U, D, L, R)
        |
        v
Hardware FIFO (4 bytes per packet: [U, D, L, R])
        |
        v
Driver reads FIFO via I2C (chunks of up to 8 packets per transaction)
        |
        v
Ratio calculation: (U-D)*100/(U+D), (L-R)*100/(L+R)
        |
        v
Averaged edges: avg(last N packets) - avg(first N packets), N=2
        |
        v
Determine direction from the edge deltas
```

### Ratio Calculation

For each FIFO packet, two ratios are computed using integer arithmetic:

```
UD_ratio = (U - D) * 100 / (U + D)    range: -100 to +100
LR_ratio = (L - R) * 100 / (L + R)    range: -100 to +100
```

- **Positive UD_ratio**: object closer to Up photodiode
- **Negative UD_ratio**: object closer to Down photodiode
- **Positive LR_ratio**: object closer to Left photodiode
- **Negative LR_ratio**: object closer to Right photodiode

### Edge Difference

The gesture direction is the difference between the **averaged edges** of the
valid-packet ratio sequence:

```
ud_delta = avg(UD_ratio of last N packets) - avg(UD_ratio of first N packets)
lr_delta = avg(LR_ratio of last N packets) - avg(LR_ratio of first N packets)
```

with N = 2. Mathematically this is equivalent to the previous "sum of ratio
deltas" (that sum telescopes to `last - first`), but the averaged edges are
robust against a single noisy frame at the swipe boundary deciding the whole
gesture. This works for both fast and slow gestures: a swipe produces a
monotonic ratio shift between its start and end.

### Direction Decision

After all FIFO data is processed (GVALID goes low):

1. Check minimum packet count (>= 4) to filter noise
2. Compare absolute edge deltas: `|ud_delta|` vs `|lr_delta|`
3. The dominant axis determines the gesture:
   - `|ud_delta| > |lr_delta|` → vertical gesture (UP or DOWN)
   - `|lr_delta| > |ud_delta|` → horizontal gesture (LEFT or RIGHT)
4. Sign determines direction within the axis

### Filtering

- **Saturation filter**: packets with any channel > 250 are discarded
- **Noise filter**: packets with all channels < 10 are discarded
- **Minimum packets**: gesture requires >= 4 valid packets

## Calibration

The driver performs automatic calibration of proximity thresholds during `apds_init()`. This ensures optimal gesture detection regardless of ambient conditions.

### How It Works

1. Takes 32 PDATA samples in gesture mode
2. Filters out saturated values (>200)
3. Computes median and standard deviation
4. Sets GPENTH (entry) and GEXTH (exit) thresholds
5. Stores thresholds for use by `sensor_reinit()`

If an I2C fault interrupts the sampling phase, collection stops early and
default thresholds are used when fewer than half of the samples remain valid —
`apds_init()` still succeeds (the sensor is already configured at that point).

### Calibration Parameters

```c
#define APDS_ENABLE_CALIBRATION     1   // Enable/disable calibration
#define APDS_CAL_SAMPLES            32  // Number of samples
#define APDS_CAL_SIGMA_COEFF        3   // Sigma coefficient (2-5)
#define APDS_CAL_PROX_MIN           10  // Minimum threshold
#define APDS_CAL_PROX_MAX           200 // Maximum threshold
#define APDS_CAL_FILTER_MAX         200 // Outlier filter threshold
```

### Thresholds

| Register | Purpose | Gesture Mode | Proximity Mode |
|----------|---------|--------------|----------------|
| GPENTH | Entry threshold | median/4 | median + 3*sigma |
| GEXTH | Exit threshold | GPENTH * 0.6 | GPENTH * 0.6 |

`PIHT`/`PILT`/`PERS` are not written: the driver never enables proximity
interrupts (`PIEN`), so those registers would have no effect. `APDS_LED_BOOST`
(CONFIG2, default 300%) scales the actual LED current and is the primary
reason for PDATA saturation at close range.

## Configuration

Defaults live in `src/apds9960_config.h`. Override them for the complete build
through PlatformIO `build_flags`:

```ini
build_flags =
    -Isrc
    -DAPDS_GAIN=3
    -DAPDS_LED_CURRENT=0
    -DAPDS_LED_BOOST=3
    -DAPDS_GGAIN=3
    -DAPDS_GLDRIVE=0
    -DAPDS_PROX_THRESHOLD=50
    -DAPDS_GESTURE_EXIT_TH=30
    -DAPDS_GWTIME=1
    -DAPDS_GESTURE_TIMEOUT_MS=300
    -DAPDS_INT_MODE=1
    -DAPDS_OWN_SYSTICK=1
```

Defining these values in `main.c` before `#include "apds9960.h"` does **not**
configure separately compiled `apds9960.c`. `apds9960_config.h` validates all
supported values during compilation. It also exposes `APDS_FIFO_SIGNAL_MIN`,
`APDS_FIFO_SATURATION_MAX`, `APDS_GESTURE_SENSITIVITY`, and `APDS_RETRY_LIMIT`.

`APDS_GESTURE_TIMEOUT_MS` bounds one `apds_readGesture()` call (1..1000 ms).
With the default `APDS_OWN_SYSTICK=1` the driver reconfigures SysTick as a
free-running counter for the duration of the call — do not use this option
if the application keeps its own SysTick timebase (set it to `0` then; the
loop is bounded by an iteration budget instead, each iteration ≥ 1 ms).

Gesture direction is decoded as the difference of **averaged edges**:
`delta = avg(last N valid packets) − avg(first N valid packets)` per axis
(N = 2). This is mathematically equivalent to the previous telescoped ratio
sum, but a single noisy frame at a swipe boundary no longer decides the whole
gesture. The metric change means `APDS_GESTURE_SENSITIVITY` values tuned for
older versions may need re-tuning (typical working range 5..40).

### Tuning Tips

| Problem | Solution |
|---------|----------|
| Gestures not detected | Increase `APDS_GAIN` and `APDS_GGAIN` to 3 (8x) |
| False positives | Increase proximity threshold or gesture sensitivity |
| Slow gestures not working | Decrease `APDS_GESTURE_SENSITIVITY` through `build_flags` (default: 5) |
| Multiple gestures per swipe | `apds_readGesture()` waits until GVALID actually clears, bounded by `APDS_GESTURE_TIMEOUT_MS`. If it still happens, check I2C signal integrity at 400 kHz. |
| Calibration fails | Check sensor orientation, ensure no direct sunlight |

### Power States

| Function | ENABLE | Power | Use case |
|----------|--------|-------|----------|
| `apds_init()` | PON+PEN+GEN+WEN | Full active | Normal operation |
| `apds_sleep()` | PON only | ~1 µA | Quick wakeup, oscillator stays on |
| `apds_shutdown()` | 0x00 | <1 µA | Maximum savings, IR LED off |

After `apds_shutdown()`, call `apds_wakeup()` to restore full operation. It handles both wake-from-sleep and wake-from-shutdown correctly.

Enable debug output by uncommenting in `apds9960.h`:

```c
#define APDS9960_DEBUG
```

Or define at build time:

```ini
build_flags = -DAPDS9960_DEBUG
```

Example output:

```
APDS9960: ID=0x9E
CAL: median=191 valid=32 sigma=0 gpenth=47 gexth=28
Gesture: LEFT
```

## Memory Footprint

| Resource | Driver only | Full demo project | Limit |
|----------|-------------|-------------------|-------|
| RAM | ~32 bytes static | ~512 bytes | 2048 bytes |
| Flash | ~2.4 KB | ~11.9 KB | 16384 bytes |

Driver-only numbers are `.text` sizes of `apds9960.o` + `int_config.o`
(measured with toolchain `size`) and exclude the I2C library (`I2C-CH32V003`),
`main.c`, startup code, and framework libraries. Full project includes all of
the above plus `printf`-based debug output. Peak stack depth inside
`apds_readGesture()` (driver + I2C call chain) is ~148 bytes against the
256-byte stack reservation of the stock linker script.

## Limitations

- `apds_readGesture()` blocks for the duration of the gesture (up to `APDS_GESTURE_TIMEOUT_MS`, default 300 ms)
- No RGB, ALS, or proximity-only API (gesture mode only)
- Calibration works best in gesture mode (proximity-only saturates)
- `GINT` is cleared by fully draining the FIFO, not by reading `GSTATUS`; there is no dedicated gesture-interrupt clear command
- With `APDS_OWN_SYSTICK=1` (default) SysTick is reconfigured by the driver during `apds_readGesture()`; applications needing their own SysTick timebase must set it to `0`

## License

[MIT](LICENSE)
