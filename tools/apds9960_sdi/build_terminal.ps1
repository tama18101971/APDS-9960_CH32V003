param([switch]$Upload)

$ErrorActionPreference = 'Stop'
$core = $env:PLATFORMIO_CORE_DIR
if (!$core) { $core = Join-Path $env:USERPROFILE '.platformio' }

pio run -d $PSScriptRoot -e apds9960_terminal
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$build = Join-Path $PSScriptRoot '.pio/build/apds9960_terminal'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$app = Join-Path $build 'GestureTerminal.exe'
$compilerArgs = @(
    '/nologo', '/target:winexe', '/codepage:65001', "/out:$app"
    '/reference:System.Windows.Forms.dll', '/reference:System.Drawing.dll'
    (Join-Path $PSScriptRoot 'GestureTerminal.cs')
)
& $compiler @compilerArgs
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($Upload) {
    $wlink = Join-Path $core 'packages/tool-wlink/wlink.exe'
    & $wlink --chip CH32V003 flash --enable-sdi-print (Join-Path $build 'firmware.bin')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
Write-Output $app
