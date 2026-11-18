# APDS-9960_CH32V003 v1.5.0

Set the sensor's mounting angle at runtime with `apds_setRotation(degrees)` and
read it with `apds_getRotation()`. Angles are 0°, 90°, 180° and 270° clockwise
when viewed from the optical face. At 0° the sensor follows Figure 11 of the
datasheet, with the IR LED on the left and pin 1 at the upper right.
`apds_readGesture()` reports directions in the observer's frame.

The source package includes the Windows SDI gesture terminal, its CH32V003
firmware, the build script, sensor diagnostics, and guided gesture testing.
The terminal starts and stops a test, switches between polling and hardware
INT on PC3, changes the orientation, and displays the library's decoded
direction. [Build and use the terminal](tools/apds9960_sdi/README.md).

## Upgrade

Existing callers use 0° until they set a rotation. Sensor reinitialization
preserves the angle. An MCU reset restores 0°. I2C-CH32V003 stays pinned
to v7.2.1. Update both the terminal and its matching firmware when upgrading
an older copy of the SDI tool.

## Кратко по-русски

Библиотека возвращает направления жестов с учётом угла монтажа. Пакет
включает исходники Windows-терминала и прошивки CH32V003. В терминале
можно запускать и останавливать распознавание, переключать опрос и
аппаратный INT и менять угол во время работы.

[История изменений](CHANGELOG.md) · [Руководство по интеграции](INTEGRATION_RU.md)
