# Integrating APDS-9960 1.4.0

[Русский](INTEGRATION_RU.md) · [API/configuration](README.md)

## PlatformIO library dependency

Use NoneOS-SDK on CH32V003. Install the Community-PIO-CH32V platform if needed; do not substitute Arduino or STM32 HAL. A minimal target configuration:

```ini
[env:ch32v003f4p6_evt_r0]
platform = ch32v
board = ch32v003f4p6_evt_r0
framework = noneos-sdk
lib_deps =
    https://github.com/tama18101971/APDS-9960_CH32V003.git#v1.4.0
build_flags = -DAPDS_INT_MODE=0
```

`v1.4.0` must exist on GitHub before installation by tag. I2C-CH32V003 v7.2.1 is automatically installed from library.json. Keep one copy of the APDS library and one I2C version; do not compile the repository demo main into your application. Framework/system startup remains the target project's responsibility.

## Manual copy / shared local library

Copy the entire `src/` folder to `lib/APDS9960/src/`, or use a local PlatformIO library directory. Required core files:

- `apds9960.c/h`, `apds9960_config.h`, `apds9960_regs.h`;
- `apds9960_log.c/h` for the supplied logger/debug support;
- `int_config.c/h` when using APDS EXTI helpers (optional for pure polling).

When copying without library.json, explicitly add `https://github.com/tama18101971/I2C-CH32V003.git#v7.2.1` to target lib_deps. The supplied logger uses UART1 and requires initialization before any logging. With diagnostic flags off and no logger calls, linker GC removes its unused code. Do not replace it with unconfigured stdio if a no-heap footprint is required.

## Initialization and service loop

Include `<ch32v00x.h>`, `<debug.h>`, `"i2c.h"`, `"apds9960.h"`; include `"int_config.h"` when using its helpers. Initialize in this order:

```c
bool app_sensor_init(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200); /* Required by example/debug logging. */
    if (i2c_init(400000) != I2C_OK) return false;
    return apds_init();
}
```

The caller must handle a false return and must not proceed to sensor use after failed init. For a complete executable including fault handlers/retry behavior, use [examples/basic/main.c](examples/basic/main.c).

Call APIs only from one main context. The ISR sets a byte flag; it never reads I2C. Output pointers are non-NULL. Capture last error before the next API can replace it. `apds_available()==false` with NONE means no ready data. `apds_readGesture()==NONE` can also mean timeout, overflow or I2C fault. Let the driver resynchronize a failed stream; do not decode or flush its tail manually as another gesture.

The maintained example checks both the flag and active-low physical INT level with bounded polling. It deliberately does not implement CPU sleep; an unprotected `if(flag==0) WFI` can lose a wakeup. Revisit system-level sleep design separately if needed.

## Shared EXTI vector

All EXTI0..7 lines share EXTI7_0_IRQHandler. Default `APDS_PROVIDE_EXTI_ISR=1` supplies a strong handler overriding the SDK's looping weak fallback. To own the vector, set `-DAPDS_PROVIDE_EXTI_ISR=0` project-wide and implement:

```c
#include "int_config.h"
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    apds_handle_exti();
    /* Handle and clear any other EXTI0..7 lines here. No I2C calls. */
    apds_clear_exti();
}
```

Alternatively, keep the default handler and override the weak `apds_exti_callback()` to service other lines. Callback runs after the APDS pending bit has been cleared. It must clear any other asserted pending sources and stay within the public I2C timing contract. `apds_exti_enable/disable` acts on the whole NVIC vector, not just the sensor line.

The repository example supplies a minimal custom handler when the provide-ISR option is0, so that CI configuration is a real link fixture. Your application must still implement its own handler if it disables the library's.

## INT pin relocation

Set all six matching parameters together; this default example keeps PC3:

```ini
build_flags =
    -DAPDS_INT_PORT=GPIOC
    -DAPDS_INT_PIN=GPIO_Pin_3
    -DAPDS_INT_LINE=EXTI_Line3
    -DAPDS_INT_PORT_SOURCE=GPIO_PortSourceGPIOC
    -DAPDS_INT_PIN_SOURCE=GPIO_PinSource3
    -DAPDS_INT_GPIO_CLOCK=RCC_APB2Periph_GPIOC
```

AFIO and selected port clocks are enabled by apds_exti_init. Read the already-low INT level before waiting for a new falling edge. PC1/PC2 remain the I2C library's documented default pins; APDS INT overrides do not remap I2C.

## Shared SysTick and custom delays

With OWN=1, readGesture owns and normalizes SysTick for its call, then stops it. Do not share it concurrently. With OWN=0, the driver uses an iteration budget but default Delay_Ms still resets/reprograms SysTick. To preserve application SysTick:

```ini
build_flags =
    -DAPDS_OWN_SYSTICK=0
    -DAPDS_DELAY_MS=app_apds_delay_ms
```

Link `void app_apds_delay_ms(uint32_t ms)` in application code. It must wait at least the requested duration without touching SysTick. The driver declares the selected external function. This only controls APDS delays; verify timing-resource contracts of all other components independently. Changing CPU/I2C clocks requires the relevant successful reinitialization.

## Profiles, faults and migration

All four IDs remain accepted. Default profiles apply only to gesture gain/drive for 0xA8 and 0x9E; they do not alter register addresses or proximity gain/drive. To tune manually, disable the matching profile, then set APDS_GGAIN/APDS_GLDRIVE/LED_BOOST. A profile does not certify every module with that ID. See [README](README.md) for nominal pulse currents and [hardware results](HARDWARE_TEST_RU.md).

I2C_ERR_RECOVERY means PE is disabled: fix the physical fault, obtain successful i2c_init, then reinitialize APDS. Repeated SDA-low/ARLO is not a reason to keep retrying forever or classify partial gestures as valid. wakeup restores retained configuration; power-cycling the sensor requires full init.

For1.3.x users: corrected GCONF2 and direction signs can change sensitivity/mounting behavior. Preserve error values0..5 while handling added TIMEOUT6 and INVALID_ARG7. Remove post-gesture discard loops. Calibration may succeed by writing fallback thresholds; it does not promise new independent samples in every background condition.

## Verification and package boundary

Build the target project, inspect actual linked Flash/RAM, and test the first gesture after sleep, timeout, overflow and reinitialization. The measured demo footprint is not your application's total. Full stack including your ISR/callback remains your responsibility.

The library package excludes examples/tests/logs/editor/CI files. The GitHub repository retains them for reproducibility. Native host regression uses the installed pinned i2c.h and does not test I2C implementation. Original0xAB/alternative 0x9C boards and broad optical conditions were not physically qualified in this release. Version and migration details: [CHANGELOG.md](CHANGELOG.md).
