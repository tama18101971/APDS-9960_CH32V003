# APDS-9960 gesture driver for CH32V003

Version **1.4.0** · [Русский](README_RU.md) · [Changelog](CHANGELOG.md) · MIT

Integer-only, single-instance C driver for LEFT, RIGHT, UP and DOWN gestures on CH32V003 with NoneOS-SDK. Uses hardware I2C1 through [I2C-CH32V003 v7.2.1](https://github.com/tama18101971/I2C-CH32V003/tree/v7.2.1). No Arduino, RTOS or dynamic allocation in the driver/demo. RGB/ALS and standalone proximity mode are not provided; raw proximity/status reads are available.

## Build and wiring

Requires PlatformIO with the Community-PIO-CH32V platform and its NoneOS-SDK/toolchain. Board: `ch32v003f4p6_evt_r0`, HSI+PLL at 48 MHz, 16 KiB program Flash and 2 KiB SRAM.

```sh
pio pkg install --platform https://github.com/Community-PIO-CH32V/platform-ch32v.git
pio run
pio run -t upload
pio device monitor --port COM6 --baud 115200
```

`COM6` was used on the test workstation; choose your actual UART port. Upload uses WCH-Link, not the UART monitor port.

| MCU | Sensor/function |
|---|---|
| PC1 | SDA |
| PC2 | SCL |
| PC3 | INT, active-low open drain |
| PD5 | UART TX,115200 baud |

Sensor address is7-bit `0x39`. SDA/SCL require pull-ups and compatible logic levels. Supply requirements and LED decoupling depend on the actual breakout; use its documentation and the [APDS-9960 datasheet](https://cdn.sparkfun.com/assets/learn_tutorials/3/2/1/Avago-APDS-9960-datasheet.pdf). Board current has not been measured.

## Use in another project

See [INTEGRATION.md](INTEGRATION.md). Pin this release in the target project's `platformio.ini`:

```ini
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.4.0
```

The tag must be published before this URL can resolve. The manifest pins the I2C dependency to `v7.2.1`. The library exports `src/`; `examples/`, tests, logs, editor configuration and CI are excluded from the package.

Initialize `SystemCoreClockUpdate()`, `Delay_Init()` and `i2c_init(400000)` before `apds_init()`. Initialize UART with `USART_Printf_Init(115200)` before using the example logger or any debug/trace mode. All sensor APIs belong to one main execution context, never an ISR; output pointers must be valid and non-NULL.

The complete maintained application is [examples/basic/main.c](examples/basic/main.c). Its default INT mode uses a bounded wait on the flag and physical INT level, with periodic polling for recovery. It intentionally does not use `WFI`; CPU sleep/power savings are not claimed. A successfully completed gesture drains its own FIFO; do not discard the next gesture with an unconditional extra read loop.

```c
/* Inside an initialized main loop; polling build: -DAPDS_INT_MODE=0. */
if (apds_available()) {
    gesture_t gesture = apds_readGesture();
    uint8_t error = apds_getLastError(); /* capture before another API call */
    if (error == APDS_ERR_NONE && gesture != GESTURE_NONE) {
        /* Deliver gesture to the application. */
    }
} else {
    uint8_t error = apds_getLastError();
    /* Handle errors or continue waiting for data. */
    (void)error;
}
```

## Supported IDs and empirical profiles

All four IDs remain accepted: **0xAB,0x9C,0xA8,0x9E**. Accepting an ID does not establish manufacturer identity or full hardware compatibility.

| ID | Default gesture gain / base LED drive | Nominal pulse current with boost300% | Evidence |
|---|---|---:|---|
| 0xAB,0x9C | 8x /100 mA |300 mA| Accepted and host-tested; not physically tested in this release |
| 0xA8 | 2x /25 mA |75 mA| Tested specimen:5 results per direction in the requested sequence; no false event during60s idle |
| 0x9E | 1x /12.5 mA |37.5 mA| First specimen recognized all four directions; a later specimen with the same ID failed communication tests |

These are board measurements and software tuning profiles, not alternate register maps or certification of every clone. Some close-range 0xA8 samples still saturated and were rejected. Exact physical pulse current was not measured. Proximity LED drive is separately controlled by `APDS_LED_CURRENT`: its default 100 mA base with 300% boost is nominally300 mA on all IDs.

`APDS_CLONE_A8_PROFILE=0` and `APDS_CLONE_9E_PROFILE=0` disable each ID-specific gain/drive override. Then `APDS_GGAIN` and `APDS_GLDRIVE` apply. `APDS_LED_BOOST` affects both proximity and gesture LED current. A damaged module or SDA held low is not a condition to hide with a new ID workaround. [Hardware evidence and limits](HARDWARE_TEST_RU.md).

## Public API

| Function | Contract |
|---|---|
| `apds_init()` | Check any supported ID, configure engines/FIFO, calibrate if enabled. `true` includes successful fallback threshold writes. |
| `apds_available()` | Check GSTATUS, resynchronize after an aborted stream, recover overflow. `false` can mean no data or error; inspect last error immediately. |
| `apds_readGesture()` | Blocking read returning a `gesture_t` in the configured orientation. I2C faults, overflow and timeouts discard partial gestures. |
| `apds_setRotation(degrees)` / `apds_getRotation()` | Set or read the mounting angle in degrees at runtime. |
| `apds_readProximity(uint8_t*)` | Raw PDATA read; does not guarantee a fresh conversion. |
| `apds_readStatus(uint8_t*)` | Raw STATUS read. |
| `apds_sleep()` | ENABLE=PON only; roughly 38 uA nominal sensor state, not a measured board value. |
| `apds_shutdown()` | ENABLE=0, oscillator/engines off; datasheet sleep current about 1 uA typical, not a clone guarantee. |
| `apds_wakeup()` | Enable retained configuration after sleep/shutdown. Next data call resynchronizes FIFO. A physical sensor power loss requires full `apds_init()`. |
| `apds_recalibrate()` | Recollect proximity data and commit thresholds/fallback; does not perform full configuration. Returns UNSUPPORTED when calibration is compiled out. |
| `apds_enableInterrupt()` / `apds_disableInterrupt()` | Update GIEN and save policy for recovery. Snapshot read/modify/write is not atomic against autonomous GMODE changes; avoid toggling mid-gesture. |
| `apds_clearInterrupt()` | Compatibility status read only; does not clear GINT. FIFO draining clears normal completion; recovery uses GCONF4.GFIFO_CLR. |
| `apds_getLastError()` | Most recent driver result. A later successful API may overwrite it. Successful void interrupt calls preserve it. |
| `apds_getLastI2CStatus()` | Sticky status of the last failed transaction; success does not clear it. |
| `apds_getReinitCount()` | Overflow-recovery counter, reset by a normal available/init/wakeup path; direct successful readGesture does not reset it. |

`gesture_t`: NONE=0, LEFT=1, RIGHT=2, UP=3, DOWN=4. Directions use the unrotated datasheet package coordinates; application mounting can rotate them.

| Driver error | Value |
|---|---:|
| `APDS_ERR_NONE` |0|
| `APDS_ERR_I2C` |1|
| `APDS_ERR_FIFO_OVERFLOW` |2|
| `APDS_ERR_SENSOR_HANG` |3|
| `APDS_ERR_INVALID_ID` |4|
| `APDS_ERR_UNSUPPORTED` |5|
| `APDS_ERR_TIMEOUT` |6|
| `APDS_ERR_INVALID_ARG` |7|

Raw I2C statuses are a different namespace: OK0,NACK1,TIMEOUT2,CLK3,BERR4,ARLO5,RECOVERY6. After RECOVERY, fix the physical condition and repeat a successful `i2c_init`; the example handles this before APDS reinitialization. The raw failure can originate from a recovery transaction rather than the original overflow.

## Algorithm, calibration and time

FIFO datasets are U,D,L,R. The driver reads at most8 datasets (32 bytes) per transaction, computes `(U-D)*100/(U+D)` and `(L-R)*100/(L+R)`, and compares the mean of the last two valid ratios with the first two. Effective minimum is `max(4,APDS_FIFO_MIN_PACKETS)`. Dominant absolute axis wins; ties select horizontal. Positive delta means DOWN/RIGHT. Averaging reduces a boundary outlier's contribution; it is not mathematically identical to a single-edge telescoping sum or immune to noise.

Packets with any channel above the saturation limit, or all channels below the signal minimum, are discarded. Return/composite gestures may cancel in the endpoint metric; NEAR/FAR is not supported.

Calibration filters up to32PDATA samples, sorts them, uses median and an integer approximation to sigma, then selects/clamps entry and exit thresholds. Insufficient valid/fresh data or a sampling fault falls back to configured defaults. Threshold writes remain strict and cache is committed after both succeed. Calibration quality depends on optics/background and clone conversion semantics; equal register reads do not prove independent conversions.

With `APDS_OWN_SYSTICK=1`, readGesture owns a free-running HCLK/8 SysTick counter and stops it on return. It checks deadline between blocking I2C calls, so actual wall time can exceed the selected milliseconds by in-flight transactions/recovery. Timeout returns NONE with TIMEOUT and marks FIFO for resynchronization. With OWN=0 the loop uses an iteration budget; this is not a precise millisecond deadline.

**OWN=0 alone does not preserve application SysTick:** default SDK `Delay_Ms` changes it. For a shared SysTick, also define `APDS_DELAY_MS` as the name of a linked `void delay(uint32_t ms)` function that waits at least the requested time without touching SysTick. Apply the define to all translation units.

## Configuration

Set macros project-wide via PlatformIO `build_flags`. A define in `main.c` does not configure separately compiled library sources. Ranges are checked in [apds9960_config.h](src/apds9960_config.h).

```ini
build_flags =
    -DAPDS_INT_MODE=0
    -DAPDS_CLONE_A8_PROFILE=0
    -DAPDS_GGAIN=1
    -DAPDS_GLDRIVE=2
```

| Macro | Default |
|---|---|
| `APDS_CLONE_A8_PROFILE` | `1` |
| `APDS_CLONE_9E_PROFILE` | `1` |
| `APDS_GAIN` | `3` |
| `APDS_LED_CURRENT` | `0` |
| `APDS_LED_BOOST` | `3` |
| `APDS_FIFO_MIN_PACKETS` | `4` |
| `APDS_PROX_THRESHOLD` | `50` |
| `APDS_GESTURE_EXIT_TH` | `30` |
| `APDS_GESTURE_TIMEOUT_MS` | `300` |
| `APDS_DELAY_MS` | `Delay_Ms` |
| `APDS_OWN_SYSTICK` | `1` |
| `APDS_GGAIN` | `3` |
| `APDS_GLDRIVE` | `0` |
| `APDS_GWTIME` | `1` |
| `APDS_FIFO_SIGNAL_MIN` | `10` |
| `APDS_FIFO_SATURATION_MAX` | `250` |
| `APDS_GESTURE_SENSITIVITY` | `5` |
| `APDS_RETRY_LIMIT` | `6` |
| `APDS_ENABLE_CALIBRATION` | `1` |
| `APDS_CAL_SAMPLES` | `32` |
| `APDS_CAL_SIGMA_COEFF` | `3` |
| `APDS_CAL_PROX_MIN` | `10` |
| `APDS_CAL_PROX_MAX` | `200` |
| `APDS_CAL_FILTER_MAX` | `200` |
| `APDS_INT_MODE` | `1` |
| `APDS_PROVIDE_EXTI_ISR` | `1` |

Gain codes: proximity/gesture0..3 =1x,2x,4x,8x. Drive codes0..3 =100,50,25,12.5 mA. Boost0..3 =100,150,200,300%. GWTIME0..7 =0,2.8,5.6,8.4,14,22.4,30.8,39.2ms. Entry/exit are different engines' thresholds; the supported config currently requires exit<=entry.

## Interrupt integration

[INTEGRATION.md](INTEGRATION.md) covers the shared vector and remapped pin/clock parameters. `apds_exti_init` configures AFIO/GPIO clock, pull-up, falling edge, EXTI and NVIC. All EXTI0..7 share one vector. Default ISR is strong; use `APDS_PROVIDE_EXTI_ISR=0` and supply your own strong handler when sharing ownership. The repository example supplies a minimal fixture for this configuration. NVIC enable/disable helpers affect the whole shared vector. User callback must clear other pending lines and remain short; no I2C calls from ISR.

## Logging and diagnostics

`apds9960_log.c/h` provides a small UART logger, supporting `%d`, `%u`, `%X`, `%02X` and `%%` only. It does not allocate or use stdio. `APDS9960_DEBUG` enables algorithm/calibration logging; initialize UART first. Production builds disable all diagnostic flags by default.

- `APDS_RAW_DEBUG`: every 8th raw dataset; disturbs timing, use short captures only.
- `APDS_I2C_TRACE`: failed wrapper register/status logging; not an audit of I2C internals.
- `APDS_HW_TEST`: brief configuration/FIFO/sleep/wakeup example checks.
- `APDS_HEALTH_TEST`: standalone example diagnostic for ID, WTIME read/write, active polling at 400/100kHz, line levels after recovery failure, then halt. Requires actual hardware and changes sensor configuration.
- `APDS_EXAMPLE_I2C_HZ`: example bus speed, default 400000; not a runtime library setting.

## Measurements and verification

Measured with GCC12.2.0, `-Os`, ch32v1.1.0+499971a, NoneOS-SDK2.30000.0+4fcf972 and I2C7.2.1 on 2026-11-17; different toolchains can change sizes.

| Scope | Flash/code bytes | RAM bytes |
|---|---:|---:|
| Full default demo |7952|380 (includes256 byte stack reservation)|
| APDS+EXTI objects before linker GC |3736 text +1 initialized data|30BSS +1 initialized data|
| Separate UART logger object |436 text|0 static|

Object totals are not exact linked contribution: unused functions are removed. Full demo before driver fixes, with the same I2C7.2.1, was12544Flash/520RAM. The library core grew to handle recovery/profiles, while removing stdio reduced whole-firmware size. readGesture's compiler-reported frame is96 bytes; full stack high-water including I2C/ISR was not measured. No unconditional peak148 byte claim is made.

```sh
pio run
python test/host/run.py
```

The host runner needs GCC and the pinned dependency header installed by PlatformIO; `CC` or `--i2c-include` can override paths. Windows wrapper: `test/host/run.ps1`. Four suites cover math, FIFO bounds, faults, profiles and lifecycle; they do not test I2C implementation or physical optics. CI builds default/polling/no-calibration/debug/custom-ISR/external-SysTick/generic profiles with `-Wextra -Werror` on project sources, and guards default RAM<=600/Flash<=13000.

Hardware evidence includes two working clone specimens and one failing0x9E module; neither chip-ID acceptance nor a60second idle test constitutes a long-term reliability certification. The original 0xAB/alternative 0x9C specimens and broad lighting/distance ranges remain untested. See [HARDWARE_TEST_RU.md](HARDWARE_TEST_RU.md).

## Migration from 1.3.x

- Recheck mounting orientation: direction signs and GCONF2 gain/drive mapping were corrected.
- ID profiles override generic gesture gain/drive by default; disable the relevant profile for manual tuning.
- Handle TIMEOUT and INVALID_ARG in addition to existing unchanged error codes0..5.
- Copy `apds9960_log.c/h` along with the other sources when integrating manually.
- Replace old WFI and post-gesture discard snippets with the maintained example.
- Shared SysTick requires both OWN=0 and a compatible delay hook.

## License

[MIT](LICENSE).
