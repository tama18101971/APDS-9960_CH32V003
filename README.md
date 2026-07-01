# APDS9960 Gesture Driver for CH32V003

Compact gesture recognition driver for the APDS9960 proximity/light/color sensor, targeting the CH32V003 microcontroller. Detects four directional gestures: **Left**, **Right**, **Up**, **Down**.

## Features

- Minimal RAM usage (~20 bytes static)
- Minimal Flash footprint (~1.5 KB excluding I2C driver)
- No dynamic memory allocation (`malloc`/`free`)
- No floating-point math — integer-only arithmetic
- Configurable parameters via `#define`
- Auto-recovery on FIFO overflow
- Clone-compatible ID check (supports original + Chinese clones)
- Platform-agnostic C code (no HAL, no Arduino)

## Hardware

### Supported

| Component | Model |
|-----------|-------|
| Microcontroller | CH32V003 (WCH) |
| Sensor | APDS9960 (including clones) |
| Interface | I2C (100 kHz Standard Mode) |

### Pin Connections

| CH32V003 | APDS9960 | Function |
|----------|----------|----------|
| PC1 | SDA | I2C Data |
| PC2 | SCL | I2C Clock |
| PD5 | — | UART TX (debug output) |

Sensor I2C address: `0x39`

## File Structure

```
src/
  apds9960.h         — Public API and configurable parameters
  apds9960.c         — Driver implementation
  apds9960_regs.h    — Register map and bit definitions
  i2c.h / i2c.c      — I2C driver for CH32V003 (existing)
  main.c             — Usage example
```

## Build

Requires [PlatformIO](https://platformio.org/) with the `ch32v` platform.

```bash
pio run
pio run -t upload
```

## API

```c
#include "apds9960.h"

// Initialize sensor (returns false if not found)
bool apds_init(void);

// Put sensor to sleep (~1 uA consumption)
bool apds_sleep(void);

// Wake sensor from sleep
bool apds_wakeup(void);

// Check if gesture data is ready
bool apds_available(void);

// Read detected gesture (blocking, ~10-60 ms)
gesture_t apds_readGesture(void);
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

## Usage Example

```c
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    i2c_init(100000);

    if (!apds_init()) {
        printf("Sensor not found!\r\n");
        while (1) {}
    }

    printf("Ready. Wave your hand!\r\n");

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

            // Cooldown: ignore data while hand moves away
            if (g != GESTURE_NONE) {
                Delay_Ms(300);
                while (apds_available()) apds_readGesture();
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
Driver reads FIFO via I2C
        |
        v
Ratio calculation: (U-D)*100/(U+D), (L-R)*100/(L+R)
        |
        v
Accumulate changes between consecutive packets
        |
        v
Determine direction from accumulated deltas
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

### Accumulation

Between consecutive valid packets, the change in ratio is accumulated:

```
ud_acc += UD_ratio[current] - UD_ratio[previous]
lr_acc += LR_ratio[current] - LR_ratio[previous]
```

This accumulation approach works for both fast and slow gestures — even small per-packet changes add up over time.

### Direction Decision

After all FIFO data is processed (GVALID goes low):

1. Check minimum packet count (>= 4) to filter noise
2. Compare absolute accumulated values: `|ud_acc|` vs `|lr_acc|`
3. The dominant axis determines the gesture:
   - `|ud_acc| > |lr_acc|` → vertical gesture (UP or DOWN)
   - `|lr_acc| > |ud_acc|` → horizontal gesture (LEFT or RIGHT)
4. Sign determines direction within the axis

### Filtering

- **Saturation filter**: packets with any channel > 250 are discarded
- **Noise filter**: packets with all channels < 10 are discarded
- **Minimum packets**: gesture requires >= 4 valid packets

## Configuration

Override defaults by defining before including `apds9960.h`:

```c
#define APDS_GAIN               3   // Proximity gain: 0=1x, 1=2x, 2=4x, 3=8x
#define APDS_LED_CURRENT        0   // LED current: 0=100mA, 1=50mA, 2=25mA, 3=12.5mA
#define APDS_GGAIN              3   // Gesture gain: 0=1x, 1=2x, 2=4x, 3=8x
#define APDS_GLDRIVE            0   // Gesture LED: 0=100mA, 1=50mA, 2=25mA, 3=12.5mA
#define APDS_PROX_THRESHOLD     50  // Proximity enter threshold (0-255)
#define APDS_GESTURE_EXIT_TH    30  // Proximity exit threshold (0-255)
#define APDS_GWTIME             1   // Gesture wait: 0=0ms, 1=2.8ms, ..., 7=39.2ms
```

### Tuning Tips

| Problem | Solution |
|---------|----------|
| Gestures not detected | Increase `APDS_GAIN` and `APDS_GGAIN` to 3 (8x) |
| False positives | Increase proximity threshold or gesture sensitivity |
| Slow gestures not working | Decrease `SENSITIVITY_1` in `apds9960.c` (default: 5) |
| Multiple gestures per swipe | Increase cooldown delay in `main.c` (default: 300 ms) |

## Memory Footprint

| Resource | Used | Limit |
|----------|------|-------|
| RAM (static) | ~20 bytes | 64 bytes |
| Flash | ~1.5 KB | 2 KB |

## Limitations

- Blocking API — `apds_readGesture()` blocks for the duration of the gesture
- No interrupt-driven mode (polling only)
- No RGB, ALS, or proximity-only API (gesture mode only)

## License

MIT
