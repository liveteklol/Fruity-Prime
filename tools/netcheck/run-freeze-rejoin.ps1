param(
    [ValidateSet('Monitor', 'Rejoined')]
    [string]$FaultTarget = 'Monitor',
    [ValidateRange(1, 2147483647)]
    [int]$Seed = 4773,
    [ValidateRange(180, 600)]
    [int]$MonitorSeconds = 210,
    [ValidateRange(90, 300)]
    [int]$RejoinedSeconds = 150,
    [ValidateRange(15, 90)]
    [int]$OldOccupantSeconds = 30
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskGame = Join-Path $taskRoot 'tools/build/out/msvc-Release'
$taskExe = Join-Path $taskGame 'FruityPrime.exe'
$taskOut = Join-Path $taskGame ("srp-g6-fault-{0}-seed-{1}-{2}" -f $FaultTarget.ToLowerInvariant(), $Seed, (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (!(Test-Path -LiteralPath $taskExe -PathType Leaf)) {
    throw "MSVC Release executable not found: $taskExe"
}

New-Item -ItemType Directory -Path $taskOut | Out-Null
$probe = [Net.Sockets.UdpClient]::new(0)
$port = $probe.Client.LocalEndPoint.Port
$probe.Dispose()
$rotation = Join-Path $taskOut 'rotation.txt'
[IO.File]::WriteAllText($rotation, 'TEST ARENA | Battle | 7 | 99')
$serverLog = Join-Path $taskOut 'server.log'
$processes = @()
$previousPhase = $env:MPHREAD_PHASE

function Start-NetcheckClient([string]$Name, [string]$Hunter, [int]$Seconds, [string]$OutputName, [string]$ErrorName, [bool]$ApplyFault) {
    $arguments = @('-netcheck', '127.0.0.1', '-port', $port, '-name', $Name,
        '-hunter', $Hunter, '-seconds', $Seconds, '-noupdate')
    if ($ApplyFault) {
        $arguments += @('-netlag', '250:40', '-netloss', '5', '-netseed', $Seed,
            '-netreorder', '10', '-netduplicate', '5')
    }
    $process = Start-Process $taskExe -ArgumentList $arguments -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $taskOut $OutputName) `
        -RedirectStandardError (Join-Path $taskOut $ErrorName)
    $null = $process.Handle
    $script:processes += $process
    return $process
}

function Wait-ForServerText([string]$Text, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($script:server.HasExited) { throw "Dedicated server exited while waiting for '$Text'" }
        $found = $false
        if (Test-Path -LiteralPath $serverLog -PathType Leaf) {
            $found = [bool](Select-String -LiteralPath $serverLog -SimpleMatch $Text -Quiet)
        }
        if (!$found) { Start-Sleep -Milliseconds 100 }
    } until ($found -or [DateTime]::UtcNow -ge $deadline)
    if (!$found) { throw "Server did not report '$Text' within $TimeoutSeconds seconds" }
}

try {
    $server = Start-Process $taskExe -ArgumentList @(
        '-server', '-port', $port, '-players', '8', '-rotation', ('"' + $rotation + '"'),
        '-nomaster', '-noautoupdate', '-noupdate') -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError (Join-Path $taskOut 'server.err')
    $null = $server.Handle
    $processes += $server
    Wait-ForServerText 'listening on UDP' 20

    $env:MPHREAD_PHASE = 'Afflict'
    $attacker = Start-NetcheckClient 'FreezeFaultNoxus' 'Noxus' $RejoinedSeconds 'attacker.txt' 'attacker.err' $false
    Start-Sleep -Milliseconds 500
    $old = Start-NetcheckClient 'FreezeFaultOld' 'Samus' $OldOccupantSeconds 'old.txt' 'old.err' $false
    Start-Sleep -Milliseconds 500
    $monitorFaulted = $FaultTarget -eq 'Monitor'
    $monitor = Start-NetcheckClient 'FreezeFaultMonit' 'Samus' $MonitorSeconds 'monitor.txt' 'monitor.err' $monitorFaulted

    $oldDeadline = [DateTime]::UtcNow.AddSeconds($OldOccupantSeconds + 30)
    do {
        if ($old.HasExited) { break }
        Start-Sleep -Milliseconds 100
    } until ([DateTime]::UtcNow -ge $oldDeadline)
    if (!$old.HasExited) { throw 'Old occupant client did not finish within its timeout' }
    Wait-ForServerText 'left (slot 1)' 10

    $rejoinedFaulted = $FaultTarget -eq 'Rejoined'
    $rejoined = Start-NetcheckClient 'FreezeFaultNew' 'Samus' $RejoinedSeconds 'rejoined.txt' 'rejoined.err' $rejoinedFaulted

    $clientProcesses = @($attacker, $monitor, $old, $rejoined)
    $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max($MonitorSeconds, $RejoinedSeconds) + 45)
    do {
        if ($server.HasExited) { throw 'Dedicated server exited during the freeze scenario' }
        $pending = @($clientProcesses | Where-Object { !$_.HasExited })
        if ($pending) { Start-Sleep -Milliseconds 250 }
    } until (!$pending -or [DateTime]::UtcNow -ge $deadline)
    if ($pending) { throw 'Freeze clients did not finish within the timeout' }

    # NetSession opens per-client diagnostics automatically. Preserve the
    # two reports involved in slot replacement beside this run's acceptance
    # artifacts so timer mismatches can be tied to snapshot/life frames.
    foreach ($clientName in @('FreezeFaultMonit', 'FreezeFaultNew')) {
        $netLog = Join-Path $taskGame ("netlog-{0}.txt" -f $clientName)
        if (Test-Path -LiteralPath $netLog -PathType Leaf) {
            Copy-Item -LiteralPath $netLog -Destination (Join-Path $taskOut ("{0}.netlog.txt" -f $clientName))
        }
    }

    $checker = Join-Path $taskRoot 'tools/netcheck/freeze_acceptance.py'
    & py -3 $checker (Join-Path $taskOut 'monitor.txt') (Join-Path $taskOut 'rejoined.txt') `
        --require FreezeFaultMonit,1,2,2 --require FreezeFaultNew,1,2,0
    $checkerExit = $LASTEXITCODE

    Write-Output "FAULT_TARGET=$FaultTarget"
    Write-Output "SERVER_PORT=$port"
    Write-Output "ARTIFACTS=$taskOut"
    Write-Output "NETCHECK_EXIT_CODES attacker=$($attacker.ExitCode) monitor=$($monitor.ExitCode) old=$($old.ExitCode) rejoined=$($rejoined.ExitCode)"
    Write-Output "FREEZE_ACCEPTANCE_EXIT=$checkerExit"
    if ($checkerExit -ne 0) { exit $checkerExit }
}
finally {
    if ($null -eq $previousPhase) { Remove-Item Env:MPHREAD_PHASE -ErrorAction SilentlyContinue }
    else { $env:MPHREAD_PHASE = $previousPhase }
    foreach ($process in $processes) {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
