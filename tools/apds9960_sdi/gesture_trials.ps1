param(
    [string]$Port = 'COM12',
    [ValidateRange(1, 20)][int]$Repetitions = 5,
    [ValidateRange(5, 300)][int]$IdleSeconds = 30,
    [string]$Log = "$PSScriptRoot/.pio/gesture-trials.log"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Mapping for the tested orientation, as seen by the person facing the sensor.
$directions = @(
    @{ Cue = 'ВПРАВО  →'; Expected = 'DOWN' },
    @{ Cue = 'ВНИЗ  ↓'; Expected = 'LEFT' },
    @{ Cue = 'ВЛЕВО  ←'; Expected = 'UP' },
    @{ Cue = 'ВВЕРХ  ↑'; Expected = 'RIGHT' }
)
$trials = @(for ($repeat = 0; $repeat -lt $Repetitions; $repeat++) {
    foreach ($offset in 0, 1, 2, 3) { $directions[($repeat + $offset) % 4] }
})

$window = [Windows.Forms.Form]::new()
$window.Text = 'APDS-9960 — проверка жестов'
$window.ClientSize = [Drawing.Size]::new(640, 320)
$window.StartPosition = 'CenterScreen'
$window.TopMost = $true

$help = [Windows.Forms.Label]::new()
$help.SetBounds(20, 16, 600, 88)
$help.Text = "Стрелки показаны относительно тебя. Держи ладонь в 4–7 см от датчика. " +
    "На зелёную стрелку сделай один прямой взмах мимо датчика. " +
    "Возвращай ладонь в исходное положение подальше от датчика."
$help.Font = [Drawing.Font]::new('Segoe UI', 12)
$window.Controls.Add($help)

$cue = [Windows.Forms.Label]::new()
$cue.SetBounds(20, 112, 600, 115)
$cue.TextAlign = 'MiddleCenter'
$cue.Font = [Drawing.Font]::new('Segoe UI', 24, [Drawing.FontStyle]::Bold)
$cue.Text = 'Подключение к COM-порту…'
$window.Controls.Add($cue)

$button = [Windows.Forms.Button]::new()
$button.SetBounds(240, 252, 160, 42)
$button.Text = 'Начать проверку'
$button.Enabled = $false
$window.Controls.Add($button)

$clock = [Diagnostics.Stopwatch]::StartNew()
$script:phase = 'ready'
$script:pending = ''
$script:index = 0
$script:matched = 0
$script:stray = 0
$script:faults = 0
$script:deadline = 0L
$script:results = [Collections.Generic.List[string]]::new()
$script:trialFaults = 0
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.NewLine = "`n"
$writer = [IO.StreamWriter]::new($Log, $false)
$writer.AutoFlush = $true
$timer = [Windows.Forms.Timer]::new()
$timer.Interval = 25

function Write-Trace([string]$Line) {
    $writer.WriteLine(('{0:F3} {1}' -f $clock.Elapsed.TotalSeconds, $Line))
}

function Set-Phase([string]$Phase, [int]$Milliseconds) {
    $script:phase = $Phase
    $script:deadline = $clock.ElapsedMilliseconds + $Milliseconds
}

$button.Add_Click({
    if ($script:phase -eq 'done') {
        $window.Close()
        return
    }
    $button.Enabled = $false
    Write-Trace "IDLE_BEGIN duration=$IdleSeconds"
    Set-Phase idle ($IdleSeconds * 1000)
})

$timer.Add_Tick({
    try {
        $script:pending += $serial.ReadExisting()
    } catch {
        Write-Trace "HOST_ERROR $($_.Exception.Message)"
        $cue.Text = 'Ошибка COM-порта'
        $timer.Stop()
        return
    }
    $lines = $script:pending.Split("`n")
    $script:pending = $lines[-1]
    for ($i = 0; $i -lt $lines.Length - 1; $i++) {
        $line = $lines[$i].TrimEnd()
        if (!$line) { continue }
        Write-Trace $line
        if ($line -match '^PROX low=\d+ high=\d+$') {
            if ($script:phase -eq 'ready') {
                $cue.Text = 'Связь с датчиком есть'
                $button.Enabled = $true
            }
        }
        if ($script:phase -in 'ready', 'done') { continue }
        if ($line -match '^FAULT |^RESULT \w+ error=[1-9]') {
            $script:faults++
            if ($script:phase -eq 'go') { $script:trialFaults++ }
        } elseif ($line -match '^RESULT (LEFT|RIGHT|UP|DOWN) error=0 raw=0$') {
            if ($script:phase -eq 'go') { $script:results.Add($Matches[1]) }
            else { $script:stray++ }
        }
    }

    $remaining = $script:deadline - $clock.ElapsedMilliseconds
    if ($script:phase -eq 'idle' -and $remaining -gt 0) {
        $cue.Text = "Измерение фона: $([Math]::Ceiling($remaining / 1000)) с"
        return
    }
    if ($remaining -gt 0) { return }
    if ($script:phase -eq 'go') {
        $expected = $trials[$script:index].Expected
        $observed = $script:results -join ','
        $pass = $script:results.Count -eq 1 -and $observed -eq $expected -and
            $script:trialFaults -eq 0
        if ($pass) { $script:matched++ }
        $script:index++
        Write-Trace "TRIAL n=$script:index expected=$expected observed=$observed faults=$script:trialFaults pass=$([int]$pass)"
        $cue.BackColor = [Drawing.Color]::Transparent
        if ($script:index -eq $trials.Count) {
            Write-Trace "SUMMARY matched=$script:matched total=$($trials.Count) stray=$script:stray faults=$script:faults"
            $cue.Text = "Результат $script:matched из $($trials.Count)"
            $button.Text = 'Закрыть'
            $button.Enabled = $true
            Set-Phase done 0
            $timer.Stop()
            $serial.Close()
            return
        }
        Set-Phase between 0
    }
    if ($script:phase -in 'idle', 'between') {
        if ($script:phase -eq 'idle') { Write-Trace 'IDLE_END' }
        $cue.Text = "Жди стрелку ($($script:index + 1)/$($trials.Count))"
        Set-Phase prepare 2200
    } elseif ($script:phase -eq 'prepare') {
        $script:results.Clear()
        $script:trialFaults = 0
        $cue.BackColor = [Drawing.Color]::PaleGreen
        $cue.Text = "СЕЙЧАС`n$($trials[$script:index].Cue)"
        Write-Trace "GO n=$($script:index + 1) expected=$($trials[$script:index].Expected)"
        [Media.SystemSounds]::Asterisk.Play()
        Set-Phase go 5500
    }
})

try {
    $serial.Open()
    $timer.Start()
    [void]$window.ShowDialog()
} finally {
    $timer.Dispose()
    $serial.Dispose()
    $writer.Dispose()
    $window.Dispose()
}
