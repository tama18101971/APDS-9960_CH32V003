# Suggested Commands

Shell is PowerShell (pwsh) on Windows; `&&` is unavailable — chain with `;` or `cmd1; if ($?) { cmd2 }`.
`pio` is on PATH via `C:\Users\tamag\.platformio\penv\Scripts\pio.exe`.

## Build / flash

```powershell
pio run                   # build default env ch32v003f4p6_evt_r0
pio run -t upload         # flash via WCH-Link
pio run -t clean
pio run -v                # shows full gcc command line (only way to confirm effective -D set)
pio device monitor -b 115200   # UART on PD5; no monitor_speed in platformio.ini, pass -b explicitly
```

Build output is scraped, not read whole — PowerShell filter for the useful lines:

```powershell
pio run 2>&1 | Select-String -Pattern "RAM:|Flash:|error|SUCCESS|FAILED"
```

## Testing a different driver config WITHOUT editing platformio.ini

`PLATFORMIO_BUILD_FLAGS` is **appended** to the ini `build_flags` (verified: `-Isrc` is preserved), so it is the correct way to probe config variants:

```powershell
$env:PLATFORMIO_BUILD_FLAGS="-DAPDS_INT_MODE=0 -DAPDS_GESTURE_SENSITIVITY=8"; pio run
Remove-Item Env:PLATFORMIO_BUILD_FLAGS      # env var persists for the whole shell session — always clear it
```

Rebuild once with the variable cleared afterwards, otherwise `.pio/build/` is left holding a non-default firmware.

## Tests

No unit tests. `test/` is an empty PlatformIO placeholder; `pio test` is not configured and requires hardware. Verification is a build + manual gesture check over UART.
