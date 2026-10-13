param(
    [ValidateRange(45, 600)]
    [int]$Seconds = 120,
    [int]$Seed = 4801,
    [string]$Room = 'MP4 HIGHGROUND'
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskGame = Join-Path $taskRoot 'tools/build/out/msvc-Release'
$taskExe = Join-Path $taskGame 'FruityPrime.exe'
$taskOut = Join-Path $taskGame ("srp-g5-pickup-fault-seed-{0}-{1}" -f $Seed, (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (!(Test-Path -LiteralPath $taskExe -PathType Leaf)) {
    throw "MSVC Release executable not found: $taskExe"
}

New-Item -ItemType Directory -Path $taskOut | Out-Null
$probe = [Net.Sockets.UdpClient]::new(0)
$port = $probe.Client.LocalEndPoint.Port
$probe.Dispose()
$rotation = Join-Path $taskOut 'rotation.txt'
    [IO.File]::WriteAllText($rotation, ('{0} | Battle | 7 | 99' -f $Room))
$processes = @()
$previousPhase = $env:MPHREAD_PHASE
try {
    $server = Start-Process $taskExe -ArgumentList @(
        '-server', '-port', $port, '-players', '8', '-rotation', ('"' + $rotation + '"'),
        '-nomaster', '-noautoupdate', '-noupdate') -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $taskOut 'server.log') `
        -RedirectStandardError (Join-Path $taskOut 'server.err')
    $null = $server.Handle
    $processes += $server

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($server.HasExited) { throw 'Dedicated server exited before it became ready' }
        $ready = Select-String -LiteralPath (Join-Path $taskOut 'server.log') -SimpleMatch 'listening on UDP' -Quiet
        if (!$ready) { Start-Sleep -Milliseconds 100 }
    } until ($ready -or [DateTime]::UtcNow -ge $deadline)
    if (!$ready) { throw 'Dedicated server did not become ready within 20 seconds' }

    $env:MPHREAD_PHASE = 'Pickup'
    $fast = Start-Process $taskExe -ArgumentList @(
        '-netcheck', '127.0.0.1', '-port', $port, '-name', ("PickupFast{0}" -f $Seed),
        '-hunter', 'Samus', '-seconds', $Seconds, '-noupdate') -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $taskOut 'control.txt') `
        -RedirectStandardError (Join-Path $taskOut 'control.err')
    $null = $fast.Handle
    $processes += $fast

    $faulted = Start-Process $taskExe -ArgumentList @(
        '-netcheck', '127.0.0.1', '-port', $port, '-name', ("PickupLag{0}" -f $Seed),
        '-hunter', 'Sylux', '-seconds', $Seconds, '-netlag', '250:40', '-netloss', '5',
        '-netseed', $Seed, '-netreorder', '10', '-netduplicate', '5', '-noupdate') `
        -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $taskOut 'faulted.txt') `
        -RedirectStandardError (Join-Path $taskOut 'faulted.err')
    $null = $faulted.Handle
    $processes += $faulted

    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds + 45)
    do {
        if ($server.HasExited) { throw 'Dedicated server exited during the pickup scenario' }
        $pending = @($processes | Select-Object -Skip 1 | Where-Object { !$_.HasExited })
        if ($pending) { Start-Sleep -Milliseconds 250 }
    } until (!$pending -or [DateTime]::UtcNow -ge $deadline)
    if ($pending) { throw 'Pickup clients did not finish within the timeout' }
    if ($fast.ExitCode -gt 1) { throw "Control client crashed with exit code $($fast.ExitCode)" }
    if ($faulted.ExitCode -gt 1) { throw "Faulted client crashed with exit code $($faulted.ExitCode)" }

    & py -3 (Join-Path $taskRoot 'tools/netcheck/pickup-rollback-acceptance.py') `
        (Join-Path $taskOut 'control.txt') (Join-Path $taskOut 'faulted.txt')
    if ($LASTEXITCODE -ne 0) { throw "Pickup rollback acceptance checker failed: $LASTEXITCODE" }

    Write-Output "SERVER_PORT=$port"
    Write-Output "ARTIFACTS=$taskOut"
    Write-Output "NETCHECK_EXIT_CODES control=$($fast.ExitCode) faulted=$($faulted.ExitCode)"
}
finally {
    if ($null -eq $previousPhase) { Remove-Item Env:MPHREAD_PHASE -ErrorAction SilentlyContinue }
    else { $env:MPHREAD_PHASE = $previousPhase }
    foreach ($process in $processes) {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
