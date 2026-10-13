param(
    [ValidateRange(45, 300)]
    [int]$Seconds = 60,
    [ValidateRange(1, 30)]
    [int]$PhaseSeconds = 3,
    [ValidateSet('', 'FormCycle', 'FormBombCycle')]
    [string]$Phase = '',
    [switch]$NoInterpolation,
    [ValidateRange(1, 2147483647)]
    [int]$Seed = 2113,
    [ValidateSet('None', 'Samus', 'Sylux', 'Kanden', 'Spire', 'Trace', 'Noxus', 'Weavel')]
    [string]$FaultHunter = 'Sylux',
    [ValidateRange(0, 1000)]
    [int]$FaultLatencyMs = 250,
    [ValidateRange(0, 1000)]
    [int]$FaultJitterMs = 40,
    [ValidateRange(0, 100)]
    [int]$FaultLossPercent = 5,
    [ValidateRange(0, 100)]
    [int]$FaultReorderPercent = 10,
    [ValidateRange(0, 100)]
    [int]$FaultDuplicatePercent = 5,
    [switch]$DebugLog,
    [string[]]$Hunters = @('Samus', 'Sylux', 'Kanden', 'Spire', 'Trace', 'Noxus', 'Weavel'),
    [string]$Room = 'TEST ARENA'
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskGame = Join-Path $taskRoot 'tools/build/out/msvc-Release'
$taskExe = Join-Path $taskGame 'FruityPrime.exe'
$taskOut = Join-Path $taskGame ("srp-g4-form-matrix-seed-{0}-{1}" -f $Seed, (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (!(Test-Path -LiteralPath $taskExe -PathType Leaf)) {
    throw "MSVC Release executable not found: $taskExe"
}

$allClients = @(
    @{ Suffix = 'Sa'; Hunter = 'Samus' },
    @{ Suffix = 'Sy'; Hunter = 'Sylux' },
    @{ Suffix = 'Ka'; Hunter = 'Kanden' },
    @{ Suffix = 'Sp'; Hunter = 'Spire' },
    @{ Suffix = 'Tr'; Hunter = 'Trace' },
    @{ Suffix = 'No'; Hunter = 'Noxus' },
    @{ Suffix = 'We'; Hunter = 'Weavel' }
)
$clients = @(
    foreach ($hunter in $Hunters) {
        $allClients | Where-Object { $_.Hunter -eq $hunter } | Select-Object -First 1
    }
)
if ($clients.Count -lt 2) {
    throw 'At least two distinct hunters are required for a form comparison.'
}
$uniqueHunterCount = @($Hunters | Select-Object -Unique).Count
$unknownHunterCount = @($Hunters | Where-Object { $_ -notin $allClients.Hunter }).Count
if ($clients.Count -ne $uniqueHunterCount -or $unknownHunterCount -gt 0) {
    throw 'Hunters must contain unique names from Samus, Sylux, Kanden, Spire, Trace, Noxus, and Weavel.'
}
if ($FaultHunter -ne 'None' -and $FaultHunter -notin $Hunters) {
    throw "FaultHunter '$FaultHunter' must be in the selected Hunters list or set to None."
}

New-Item -ItemType Directory -Path $taskOut | Out-Null
$probe = [Net.Sockets.UdpClient]::new(0)
$port = $probe.Client.LocalEndPoint.Port
$probe.Dispose()
$rotation = Join-Path $taskOut 'rotation.txt'
[IO.File]::WriteAllText($rotation, ('{0} | Battle | 7 | 99' -f $Room))
$serverLog = Join-Path $taskOut 'server.log'
$processes = @()
$clientProcesses = @()
$previousPhase = $env:MPHREAD_PHASE
$previousPhaseSeconds = $env:MPHREAD_PHASE_SECONDS
$phaseWasSet = $true

function Wait-ForServerText([string]$Text, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($script:server.HasExited) { throw "Dedicated server exited while waiting for '$Text'" }
        $found = (Test-Path -LiteralPath $script:serverLog -PathType Leaf) -and
            [bool](Select-String -LiteralPath $script:serverLog -SimpleMatch $Text -Quiet)
        if (!$found) { Start-Sleep -Milliseconds 100 }
    } until ($found -or [DateTime]::UtcNow -ge $deadline)
    if (!$found) { throw "Server did not report '$Text' within $TimeoutSeconds seconds" }
}

try {
    if ($Phase -ne '') {
        $env:MPHREAD_PHASE = $Phase
    }
    else {
        Remove-Item Env:MPHREAD_PHASE -ErrorAction SilentlyContinue
    }
    $env:MPHREAD_PHASE_SECONDS = [string]$PhaseSeconds

    $serverArguments = @(
        '-server', '-port', $port, '-players', '8', '-rotation', ('"' + $rotation + '"'),
        '-nomaster', '-noautoupdate', '-noupdate')
    if ($DebugLog) {
        $serverArguments += '-debuglog'
    }
    $server = Start-Process $taskExe -ArgumentList $serverArguments -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError (Join-Path $taskOut 'server.err')
    $script:serverLog = $serverLog
    $null = $server.Handle
    $processes += $server
    Wait-ForServerText 'listening on UDP' 20

    foreach ($client in $clients) {
        $name = 'FM{0}{1}' -f $client.Suffix, $Seed
        $arguments = @('-netcheck', '127.0.0.1', '-port', $port, '-name', $name,
            '-hunter', $client.Hunter, '-seconds', $Seconds, '-noupdate')
        if ($NoInterpolation) {
            $arguments += '-nointerp'
        }
        if ($DebugLog) {
            $arguments += '-debuglog'
        }
        if ($client.Hunter -eq $FaultHunter) {
            $arguments += @('-netlag', ("{0}:{1}" -f $FaultLatencyMs, $FaultJitterMs),
                '-netloss', $FaultLossPercent, '-netseed', $Seed,
                '-netreorder', $FaultReorderPercent, '-netduplicate', $FaultDuplicatePercent)
        }

        $report = Join-Path $taskOut ("{0}.txt" -f $client.Hunter)
        $process = Start-Process $taskExe -ArgumentList $arguments -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $report `
            -RedirectStandardError (Join-Path $taskOut ("{0}.err" -f $client.Hunter))
        $null = $process.Handle
        $clientProcesses += [pscustomobject]@{ Hunter = $client.Hunter; Process = $process; Report = $report }
        $processes += $process
    }

    Wait-ForServerText ("{0} peer(s) connected" -f $clients.Count) 60
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds + 60)
    do {
        if ($server.HasExited) { throw 'Dedicated server exited during the form matrix' }
        $pending = @($clientProcesses | Where-Object { !$_.Process.HasExited })
        if ($pending) { Start-Sleep -Milliseconds 250 }
    } until (!$pending -or [DateTime]::UtcNow -ge $deadline)
    if ($pending) { throw 'Form matrix clients did not finish within the timeout' }
    foreach ($entry in $clientProcesses) {
        if ($entry.Process.ExitCode -gt 1) {
            throw "Client $($entry.Hunter) crashed with exit code $($entry.Process.ExitCode)"
        }
    }

    $reportPaths = @($clientProcesses | ForEach-Object { $_.Report })
    $comparison = Join-Path $taskOut 'compare-reports.txt'
    & py -3 (Join-Path $taskRoot 'tools/netcheck/compare-reports.py') @reportPaths *> $comparison
    $compareExit = $LASTEXITCODE

    Write-Output "ROOM=$Room"
    Write-Output "PHASE=$(if ($Phase -eq '') { 'tour' } else { $Phase })"
    Write-Output "PHASE_SECONDS=$PhaseSeconds"
    Write-Output "DEBUG_LOGS=$(if ($DebugLog) { 'enabled' } else { 'disabled' })"
    Write-Output "INTERPOLATION=$(if ($NoInterpolation) { 'disabled' } else { 'enabled (default)' })"
    Write-Output "FAULT_HUNTER=$FaultHunter"
    if ($FaultHunter -eq 'None') {
        Write-Output 'FAULT=disabled'
    }
    else {
        Write-Output ("FAULT={0}+-{1}ms RTT, loss {2}%, reorder {3}%, duplicate {4}%" -f
            $FaultLatencyMs, $FaultJitterMs, $FaultLossPercent, $FaultReorderPercent,
            $FaultDuplicatePercent)
    }
    Write-Output "HUNTERS=$($clients.Hunter -join ',')"
    Write-Output "SERVER_PORT=$port"
    Write-Output "ARTIFACTS=$taskOut"
    foreach ($entry in $clientProcesses) {
        Write-Output "NETCHECK_EXIT_$($entry.Hunter)=$($entry.Process.ExitCode)"
    }
    Write-Output "COMPARE_REPORTS_EXIT=$compareExit"
    Get-Content -LiteralPath $comparison -Tail 35
    exit $compareExit
}
finally {
    if ($phaseWasSet) {
        if ($null -eq $previousPhase) { Remove-Item Env:MPHREAD_PHASE -ErrorAction SilentlyContinue }
        else { $env:MPHREAD_PHASE = $previousPhase }
    }
    if ($null -eq $previousPhaseSeconds) { Remove-Item Env:MPHREAD_PHASE_SECONDS -ErrorAction SilentlyContinue }
    else { $env:MPHREAD_PHASE_SECONDS = $previousPhaseSeconds }
    foreach ($process in $processes) {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
