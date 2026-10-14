<#
.SYNOPSIS
  One cell of the homing network matrix: a dedicated server and scripted
  clients firing the homing schedule at each other, every machine writing
  -homingtrace, then compare-homing-trace.py over all their logs.

.EXAMPLE
  tools\testing\run-homing-net-matrix.ps1 -Label rtt80 -Clients 2 -LagMs 80
  tools\testing\run-homing-net-matrix.ps1 -Label eight -Clients 8 -Mode PrimeHunter -LossPercent 5 -ReorderPercent 5
  tools\testing\run-homing-net-matrix.ps1 -Label bots -Clients 2 -Bots 4
#>
param(
    [string]$Label = "matrix",
    [int]$Clients = 2,
    [int]$Bots = 0,
    [int]$LagMs = 0,
    [int]$JitterMs = 0,
    [double]$LossPercent = 0,
    [double]$ReorderPercent = 0,
    [ValidateSet("Battle", "PrimeHunter")][string]$Mode = "Battle",
    [string]$Room = "TEST ARENA",
    [int]$Seconds = 90,
    [int]$Port = 27950,
    [switch]$Morph,
    [switch]$HostMode,
    [ValidateSet("MasterServer", "DedicatedServer")][string]$HostService = "MasterServer",
    [string[]]$Hunters = @("Samus", "Kanden", "Sylux", "Noxus", "Kanden", "Trace", "Samus", "Weavel"),
    [string]$Build = "$PSScriptRoot\..\build\out\msvc-Release"
)

$ErrorActionPreference = "Stop"
$build = (Resolve-Path $Build).Path
$exe = Join-Path $build "FruityPrime.exe"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $build "homing-matrix\$stamp-$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$rotation = Join-Path $out "rotation.txt"
Set-Content -Path $rotation -Value "$Room | $Mode | 30 | 999" -Encoding ascii
$started = Get-Date

$serverArgs = @("-server", "-port", $Port, "-players", 8, "-rotation", $rotation, "-nomaster",
    "-servername", "HomingMatrix", "-noupdate", "-noautoupdate", "-homingtrace")
if ($Bots -gt 0) { $serverArgs += @("-bots", $Bots) }
$directoryPort = $Port + 1
$directory = $null
if ($HostMode) {
    if ($Bots -gt 0) { throw "Host checks require human peers; use the dedicated bot cell." }
    $serverArgs = @("-masterserver", "-port", $directoryPort, "-hostports", "$Port-$Port",
        "-homingtrace", "-noupdate", "-noautoupdate")
    if ($HostService -eq "DedicatedServer") {
        $serverArgs = @("-server", "-port", $directoryPort, "-hostports", "$Port-$Port",
            "-rotation", $rotation, "-players", 8, "-nomaster", "-servername", "HomingParent",
            "-homingtrace", "-noupdate", "-noautoupdate")
    }
}
$server = $null
$clientProcesses = @()
$previousLogDirectory = $env:FRUITY_DEBUG_LOG_DIRECTORY
try {
$env:FRUITY_DEBUG_LOG_DIRECTORY = Join-Path $out 'server-logs'
$server = Start-Process -FilePath $exe -ArgumentList $serverArgs -WorkingDirectory $build -PassThru `
    -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $out "server.stdout.txt") -RedirectStandardError (Join-Path $out "server.stderr.txt")
Start-Sleep -Seconds 4
if ($HostMode) {
    $directory = $server
    $replyText = python -I "$PSScriptRoot\request-test-room.py" --port $directoryPort --room $Room --mode $Mode
    if ($LASTEXITCODE -ne 0) { throw "Hosted room request failed: $replyText" }
    $reply = $replyText | ConvertFrom-Json
    if (-not $reply.started -or $reply.port -ne $Port) { throw "Unexpected room reply: $replyText" }
    $child = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($directory.Id)" |
        Where-Object { $_.ExecutablePath -eq $exe -and $_.CommandLine -match '-server' } | Select-Object -First 1
    if (-not $child) { throw "Directory did not create a separate dedicated server process" }
    $server = Get-Process -Id $child.ProcessId
    Set-Content -Path (Join-Path $out "host-reply.json") -Value $replyText -Encoding ascii
}

$lineArgs = @()
if ($LagMs -gt 0) { $lineArgs += @("-netlag", $(if ($JitterMs -gt 0) { "${LagMs}:${JitterMs}" } else { "$LagMs" })) }
if ($LossPercent -gt 0) { $lineArgs += @("-netloss", "$LossPercent%") }
if ($ReorderPercent -gt 0) { $lineArgs += @("-netreorder", "$ReorderPercent%") }

for ($i = 0; $i -lt $Clients; $i++) {
    $hunter = $Hunters[$i % $Hunters.Count]
    $clientArgs = @("-netcheck", "127.0.0.1", "-port", $Port, "-name", "H$i$hunter", "-hunter", $hunter,
        "-headless", "-seconds", $Seconds, "-hitrig", "homing", "-homingtrace", "-noupdate") + $lineArgs
    if ($Morph) { $clientArgs += "-rigmorph" }
    $env:FRUITY_DEBUG_LOG_DIRECTORY = Join-Path $out "client$i-logs"
    $clientProcess = Start-Process -FilePath $exe -ArgumentList $clientArgs -WorkingDirectory $build -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $out "client$i.stdout.txt") -RedirectStandardError (Join-Path $out "client$i.stderr.txt")
    # Retain the process handle so Windows PowerShell can read ExitCode after exit.
    $null = $clientProcess.Handle
    $clientProcesses += $clientProcess
    Start-Sleep -Milliseconds 1500
}

$deadline = (Get-Date).AddSeconds($Seconds + 120)
foreach ($clientProcess in $clientProcesses) {
    $left = [Math]::Max(1, [int]($deadline - (Get-Date)).TotalMilliseconds)
    if (-not $clientProcess.WaitForExit($left)) { Stop-Process -Id $clientProcess.Id -Force }
}
} finally {
    $env:FRUITY_DEBUG_LOG_DIRECTORY = $previousLogDirectory
    foreach ($ownedProcess in $clientProcesses) {
        if (-not $ownedProcess.HasExited) { Stop-Process -Id $ownedProcess.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    if ($directory -and -not $directory.HasExited) { Stop-Process -Id $directory.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Seconds 2

$logs = @()
$missingLog = $false
$firstSlot = 0
$all = @(@{ Name = "server"; Process = $server }) + @(for ($i = 0; $i -lt $clientProcesses.Count; $i++) { @{ Name = "client$($i + $firstSlot)"; Process = $clientProcesses[$i] } })
foreach ($entry in $all) {
    $log = Get-ChildItem (Join-Path $out "$($entry.Name)-logs") -Filter "FruityPrime-*-$($entry.Process.Id).log" -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $started } | Select-Object -First 1
    if ($log) {
        $copy = Join-Path $out "$($entry.Name).log"
        Copy-Item $log.FullName $copy
        $logs += $copy
    } else {
        Write-Output "PROBLEM no log for $($entry.Name) (pid $($entry.Process.Id))"
        $missingLog = $true
    }
}

$description = "label=$Label clients=$Clients bots=$Bots lag=$LagMs jitter=$JitterMs loss=$LossPercent% reorder=$ReorderPercent% mode=$Mode room=$Room seconds=$Seconds morph=$Morph host=$HostMode service=$HostService"
Set-Content -Path (Join-Path $out "cell.txt") -Value $description -Encoding ascii
Write-Output $description
$requirements = @()
if ($Mode -eq "PrimeHunter") { $requirements += "--require-prime" }
python -I "$PSScriptRoot\compare-homing-trace.py" @logs @requirements --json (Join-Path $out "result.json") | Tee-Object -FilePath (Join-Path $out "compare.txt")
$code = $LASTEXITCODE
if ($missingLog) { $code = 1 }
foreach ($i in 0..($clientProcesses.Count - 1)) {
    $clientProcesses[$i].Refresh()
    $clientOutput = Get-Content (Join-Path $out "client$($i + $firstSlot).stdout.txt") -ErrorAction SilentlyContinue
    if ($clientOutput -match 'authority=True|became the simulation authority') {
        Write-Output "PROBLEM client $i became authority"
        $code = 1
    }
    if (-not ($clientOutput -match 'authority=False') -or $clientProcesses[$i].ExitCode -notin @(0, 1)) {
        Write-Output "PROBLEM client $i did not finish the expected non-authoritative check"
        $code = 1
    }
    $unexpected = @($clientOutput | Select-String -Pattern 'FAIL:' |
        Where-Object { $_.Line -notmatch 'never took a single hit' })
    if ($unexpected.Count -gt 0 -or ($clientOutput -match 'crashed:|could not join')) {
        Write-Output "PROBLEM client $($i + $firstSlot) failed outside the one-sided firing rig"
        $unexpected | ForEach-Object { Write-Output $_.Line }
        $code = 1
    }
    $tail = Get-Content (Join-Path $out "client$($i + $firstSlot).stdout.txt") -Tail 3 -ErrorAction SilentlyContinue
    Write-Output "client$i exit=$($clientProcesses[$i].ExitCode): $($tail -join ' / ')"
}
Write-Output "output: $out"
if ($code -ne 0) { Write-Output "RESULT FAIL" }
exit $code
