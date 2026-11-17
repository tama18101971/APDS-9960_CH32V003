$ErrorActionPreference = 'Stop'
$python = Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'
if (!(Test-Path $python)) { $python = 'python' }
& $python "$PSScriptRoot/run.py" @args
if ($LASTEXITCODE) { throw 'Host regression failed' }
