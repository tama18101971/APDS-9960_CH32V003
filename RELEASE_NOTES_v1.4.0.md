# APDS-9960_CH32V003 v1.4.0

Corrects gesture configuration/direction and makes FIFO/error boundaries explicit,
while retaining all four accepted IDs and the existing public function signatures.
I2C-CH32V003 is pinned to7.2.1; its implementation is not modified.

- Correct GCONF2 GGAIN/GLDRIVE and DOWN/RIGHT sign mapping.
- Hardware FIFO clear, overflow-before-decode, resync after abort/wakeup and explicit timeout.
- Calibration fallback/clamp/cache fixes; saved interrupt policy and selected GPIO clock.
- Optional measured 0xA8/0x9E gain/drive profiles, individually disabled for manual tuning.
- Compact integer UART output replaces stdio; portable host tests and expanded CI.
- English/Russian docs now describe current contracts, migration and test limits.

Default demo:7952B Flash /380B RAM (stack reservation included), measured with GCC12.2
and I2C7.2.1. Before driver fixes with the same I2C:12544B /520B.
Core+EXTI objects grew from 3368B to3736B text; the separate logger is436B, plus1B
initialized data. Whole-firmware savings come from removing stdio, not shrinking
all driver functions. Compiler-reported readGesture frame96B is not full stack peak.

## Upgrade notes

Recheck direction orientation and sensitivity. ID profiles override generic gesture
gain/drive unless disabled. Added errors:TIMEOUT6, INVALID_ARG7; existing0..5 unchanged.
Manual copying must include apds9960_log.c/h. Shared SysTick requires OWN=0 and a
compatible APDS_DELAY_MS hook. Replace old WFI/live-discard examples.

## Hardware evidence

Two working clone specimens were exercised. The0xA8 test produced5 LEFT,5 RIGHT,
5 UP,5 DOWN in order; some close-range raw samples clipped. Another0x9E module failed
communication tests and is not a supported-hardware certification result. Original
0xAB/alternative 0x9C and long soak were not physically qualified. See hardware report.

## Кратко по-русски

Исправлены поля усиления/тока и направления, FIFO/recovery/timeout, калибровка и GIEN.
Сохранены четыре ID; добавлены отключаемые профили проверенных плат0xA8/0x9E.
Прошивка demo занимает7952Б Flash/380Б RAM. Перед обновлением перепроверьте монтаж,
чувствительность, обработку новых ошибок и подключите файлы UART-логгера.
Проверка отдельных экземпляров не является гарантией всех клонов или длительной
надёжности. Сведения о неисправном модуле не скрыты новой веткой по ID.
