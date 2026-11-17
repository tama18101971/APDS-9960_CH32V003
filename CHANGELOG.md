# Changelog

## 1.4.0 — 2026-11-17

### Fixed

- Correct GCONF2 GGAIN/GLDRIVE fields and direction signs in datasheet package coordinates.
- Check GFOV before GVALID and clear hardware FIFO during configuration/recovery.
- Discard interrupted/timed-out streams and resynchronize before another result;
  timeout now returns NONE with APDS_ERR_TIMEOUT instead of a partial successful gesture.
- Write fallback thresholds during recalibration, compute exit after entry clamp,
  and commit calibration cache only after both writes succeed.
- Preserve requested GIEN policy across recovery; interrupt helpers retain the
  read GMODE value rather than unconditionally forcing gesture entry.
- Enable the selected INT GPIO clock. Replace example check-then-WFI and live
  post-gesture discard loops with bounded flag/INT-level service and error recovery.
- Normalize/stop owned SysTick; document that OWN=0 also needs a compatible
  delay hook to preserve an application's timer.

### Added

- Optional empirical profiles for IDs 0xA8 (2x/25 mA base) and 0x9E (1x/12.5 mA base).
  All IDs 0xAB/0x9C/0xA8/0x9E remain accepted; no alternate register map is assumed.
- APDS_ERR_TIMEOUT=6, APDS_ERR_INVALID_ARG=7; previous error values0..5 unchanged.
- APDS9960_VERSION, APDS_DELAY_MS hook and APDS_INT_GPIO_CLOCK override.
- Integer-only UART logger, native host regression runner and optional board diagnostics.
- External-SysTick/generic-profile CI builds and an example custom-ISR link fixture.

### Changed

- Pin I2C-CH32V003 to v7.2.1 in both manifests; consume its public API, including
  I2C_ERR_RECOVERY handling in the example. I2C implementation is unchanged.
- Replace demo/debug stdio with bounded UART formatting (no heap/float).
- Rewrite English/Russian API/integration docs, current/size claims and migration notes.
- Default demo measured 7952B Flash/380B RAM, versus12544B/520B before driver fixes
  using the same I2C7.2.1. Core+EXTI object text3736B; logger436B, plus1 initialized
  APDS data byte. Object sums are before linker GC, not exact final contribution.

### Validation and limits

Two working clone specimens were exercised;0xA8 produced five results per direction
in the requested order. Some close-range samples still saturated. A different 0x9E
specimen failed with persistent communication/SDA-low faults and was not classified
as healthy or given an ID-wide workaround. No original 0xAB/alternative 0x9C specimens,
full electrical characterization, complete branch coverage or long-term soak were run.
See HARDWARE_TEST_RU.md and test/evidence.

### Migration

Recheck mounting orientation and sensitivity; corrected fields/signs and enabled
ID profiles change behavior. Disable a matching profile for manual gain/drive tuning.
Handle added errors; copy logger files when integrating manually; replace old WFI/drain
snippets. Shared SysTick needs both OWN=0 and a delay hook. Public function signatures
and gesture/error values0..5 remain compatible.

## 1.3.0

Previous tagged release: real gesture timeout, chunked FIFO reads and averaged-edge
recognition. See the repository tag/history for the original implementation.
