# Requirements Document — APDS9960 Gesture Driver for CH32V003

## Project Overview

**Goal:** Develop a compact APDS9960 gesture sensor driver for the **CH32V003** microcontroller with reliable gesture recognition.

---

## Project Goals (Ordered by Priority)

| Priority | Requirement |
|----------|-------------|
| 1 | Reliable gesture recognition |
| 2 | Minimal power consumption |
| 3 | Minimal Flash footprint |
| 4 | Minimal RAM usage |
| 5 | Simple project integration |

---

## Supported Hardware

### Microcontroller
- **CH32V003** (WCH/ST compatible)

### Communication Interface
- **I²C** (uses existing I²C driver — do not reimplement)

### Sensor
- APDS9960 (including Chinese-compatible clones)

---

## Supported Functions

| Function | Status |
|----------|--------|
| Gesture Up | ✅ Supported |
| Gesture Down | ✅ Supported |
| Gesture Left | ✅ Supported |
| Gesture Right | ✅ Supported |
| RGB | ❌ Not supported |
| ALS (Ambient Light) | ❌ Not supported |
| Proximity API | ❌ Not supported |
| General-purpose Interrupt API | ❌ Not supported |
| Color Sensor | ❌ Not supported |

### Exception
- The internal proximity sensor may be used **only** to trigger gesture mode.

---

## API Requirements

### Minimal API Surface

```c
// Initialization and power control
bool apds_init(void);     // Initialize APDS9960 via I²C
bool apds_sleep(void);    // Put sensor in low-power sleep
bool apds_wakeup(void);   // Wake sensor from deep sleep

// Status check
bool apds_available(void); // True if gesture data is ready to read

// Read last detected gesture
gesture_t apds_readGesture(void);

typedef enum {
    GESTURE_NONE,
    GESTURE_LEFT,
    GESTURE_RIGHT,
    GESTURE_UP,
    GESTURE_DOWN
} gesture_t;
```

**Constraints:**
- No dynamic memory allocation (`malloc`/`free`)
- All functions are blocking-free (no `vTaskDelay`, no busy-wait loops > few ms)
- Return values: `true` = success / data available, `false` = error / retry needed

---

## Power Consumption Requirements

| Requirement | Detail |
|-------------|--------|
| Idle power | Minimal — use APDS9960 native sleep mode |
| Wake-up trigger | Prefer INT (interrupt) pin; auto-start FIFO on detection |
| MCU state | MCU should be in Sleep when possible |
| Post-detection | Sensor automatically begins FIFO collection after object appears |

---

## Algorithm Requirements

### Core
- Based on Avago/Broadcom algorithm but **reimplemented** for compact embedded use
- Do not copy the original library verbatim
- Must handle all FIFO packets (UDLR = 4 bytes per packet)

### Filtering
- Ignore: saturated values, too-small values, noise, single spikes
- Apply configurable thresholds (`APDS_FIFO_THRESHOLD`, etc.)

### Direction Calculation
- Use **ratios** `U/D` and `L/R` — not absolute values
- Priority order: Left → Right → Up → Down
- Or compute based on maximum accumulated delta

### Restrictions
| Forbidden | Allowed |
|-----------|---------|
| `float`, `double` | `int8_t`, `int16_t`, `uint32_t` only |
| Recursion | Iterative processing only |
| Complex math | Simple arithmetic, ratios, counters |

### Robustness
The algorithm must work consistently:
- ✅ Slow gestures
- ✅ Fast gestures
- ✅ Varying hand distances (3–15 cm)
- ✅ Different skin tones / hand colors
- ✅ Daylight & indoor lighting conditions
- ✅ Dark environments

---

## Failure Recovery Requirements

Driver **must** recover automatically when:
| Scenario | Action |
|----------|--------|
| FIFO overflow | Reset sensor, reinitialize |
| Sensor hang (no response) | Retry I²C with timeout |
| I²C error | Attempt recovery sequence |
| Sensor not responding | Full reset + retry |
| Timeout on any operation | Auto-retry up to 3 times |

---

## Clone Compatibility

| Requirement | Detail |
|-------------|--------|
| ID check | Do **not** enforce strict device ID |
| Allowed variants | Support multiple known APDS9960 clones |
| Init attempt | If ID mismatch, try initialization anyway |
| Error reporting | Report error only if device truly unresponsive |

---

## Memory Budget

| Resource | Target | Maximum |
|----------|--------|---------|
| RAM | < 64 bytes | 128 bytes |
| Flash | < 2 KB | 3 KB (excluding I²C driver) |

### Usage Breakdown Estimate
- FIFO buffer: ~96 bytes (configured by `APDS_FIFO_LENGTH`)
- Gesture state / counters: ~16–32 bytes
- Configuration table / thresholds: ~80 bytes
- Stack for reentry-safe functions: minimal
- **Total:** well under 64 bytes RAM, < 2 KB Flash

---

## Code Requirements

| Requirement | Detail |
|-------------|--------|
| `malloc()` | ❌ Forbidden |
| `printf()` | ❌ Forbidden (use UART/LED debug only if needed) |
| HAL / CMSIS | ❌ Not used (native register access or existing I²C driver) |
| Arduino core | ❌ Platform-agnostic C code |
| STL / containers | ❌ No standard library beyond `stdint.h`, `string.h` |
| Recursion | ❌ Forbidden |
| Global large arrays | ❌ Forbidden (use stack or small static buffers) |
| Blocking delays | ❌ None > few milliseconds |

### Configurable Parameters (via `#define`)

```c
#define APDS_GAIN              // Sensor gain setting
#define APDS_LED_CURRENT       // LED current for gesture mode
#define APDS_FIFO_THRESHOLD    // FIFO data threshold
#define APDS_PROX_THRESHOLD    // Internal proximity trigger level
#define APDS_GESTURE_TIMEOUT   // Max idle time before resetting state
#define APDS_ENABLE_DIAGONAL_FILTER  // Enable diagonal motion filter
```

---

## Architecture

### File Structure

```
apds9960.c          // Implementation
apds9960.h          // Public API header
apds9960_regs.h     // Register definitions
```

### I²C Abstraction

All I²C communication centralized in minimal helper functions:

| Function | Description |
|----------|-------------|
| `readReg(uint8_t addr, uint8_t reg)` | Read single register |
| `writeReg(uint8_t addr, uint8_t reg, uint8_t data)` | Write single register |
| `readBlock(uint8_t addr, uint8_t *buf, size_t len)` | Read multiple registers |
| `writeBlock(uint8_t addr, const uint8_t *buf, size_t len)` | Write multiple registers |

---

## Test Matrix

Verify the following scenarios:

### Gesture Tests
- [ ] Slow wave (3+ seconds)
- [ ] Fast wave (< 0.5 s)
- [ ] Short tap (~0.1 s)
- [ ] Long hold (> 2 s)

### Environmental Tests
- [ ] Sunlight / direct outdoor light
- [ ] Indoor ambient lighting
- [ ] Complete darkness
- [ ] White hand
- [ ] Dark-skinned hand
- [ ] Hand distance: 3 cm, 5 cm, 10 cm, 15 cm

### Stress Tests
- [ ] 1000 consecutive gestures without hang
- [ ] Recovery after sensor power-off/on cycle
- [ ] I²C bus error recovery
- [ ] Sensor freeze / timeout recovery

---

## Success Criteria

| Metric | Target |
|--------|--------|
| Gesture accuracy (5–10 cm, normal lighting) | ≥ 99% for Up/Down/Left/Right |
| False positives | ≤ 1 per 500 no-gesture samples |
| Power consumption (idle) | Minimal — sleep mode + INT wake-up |
| Driver size (Flash) | ≤ 2 KB, ideally < 1 KB (excluding I²C driver) |
| Driver size (RAM) | ≤ 64 bytes, max 128 bytes |

---

## Additional Recommendations

### 1. Automatic Threshold Adaptation
If false positives occur due to lighting or sensor characteristics, the driver should **auto-adjust internal thresholds** within small bounds without user intervention. This is critical for autonomous devices with fixed placement.

### 2. Two-Stage Direction Confirmation
Instead of deciding direction from a single FIFO packet:
1. Accumulate trend over **multiple consecutive packets** (e.g., last 4–8)
2. Compare accumulated `U/D` and `L/R` deltas against confirmation threshold

This slightly increases computation but dramatically reduces false direction detection on cheap APDS9960 clones with noisy outputs.

---

## Notes for AI/LLM Integration

- This document is structured to be parsed by AI systems for automated code generation, testing scaffolding, and CI/CD pipeline integration.
- All requirements are expressed unambiguously without subjective language (e.g., "clean", "good practice") — only measurable targets.
- Test matrix provides concrete validation cases for regression testing.

---

*Document version: 1.0*  
*Last updated: 2025*