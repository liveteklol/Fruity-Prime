# Windows counterpart of bench.py: launch a program, sample its CPU time and
# memory over a window, stop it, print one JSON object.
#   powershell -ExecutionPolicy Bypass -File bench-windows.ps1 -Label X -Exe C:\...\a.exe -Args "..." [-WorkDir DIR]
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Args = "",
    [string]$WorkDir = "",
    [double]$Warmup = 5,
    [double]$Seconds = 25,
    [string]$StdOut = ""
)
$ErrorActionPreference = "Stop"
if ($WorkDir -eq "") { $WorkDir = Split-Path -Parent $Exe }
if ($StdOut -eq "") { $StdOut = Join-Path $env:TEMP ("bench-" + [guid]::NewGuid().ToString() + ".txt") }

$launched = Get-Date
$p = Start-Process -FilePath $Exe -ArgumentList $Args -WorkingDirectory $WorkDir -PassThru -RedirectStandardOutput $StdOut
Start-Sleep -Milliseconds ([int]($Warmup * 1000))
$p.Refresh()
$cpu0 = $p.TotalProcessorTime.TotalSeconds
$t0 = Get-Date
$ws = @()
$threads = 0
$deadline = $t0.AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    Start-Sleep -Milliseconds 250
    $p.Refresh()
    if ($p.HasExited) { break }
    $ws += $p.WorkingSet64
    $threads = [Math]::Max($threads, $p.Threads.Count)
    $cpu1 = $p.TotalProcessorTime.TotalSeconds
    $t1 = Get-Date
    $peak = $p.PeakWorkingSet64
}
# The C++ --bench mode quits by itself; give it a moment.
for ($i = 0; $i -lt 40 -and -not $p.HasExited; $i++) { Start-Sleep -Milliseconds 250 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }

$wall = ($t1 - $t0).TotalSeconds
$result = [ordered]@{
    label                   = $Label
    cpu_percent_of_one_core = [Math]::Round(100 * ($cpu1 - $cpu0) / $wall, 1)
    cpu_seconds             = [Math]::Round($cpu1 - $cpu0, 2)
    wall_seconds            = [Math]::Round($wall, 2)
    rss_mb_avg              = [Math]::Round((($ws | Measure-Object -Average).Average) / 1MB, 1)
    rss_mb_peak             = [Math]::Round($peak / 1MB, 1)
    threads                 = $threads
    launched                = $launched.ToString("HH:mm:ss.fff")
    stdout                  = $StdOut
}
$result | ConvertTo-Json -Compress
