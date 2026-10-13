param(
    [string]$Executable = 'tools/build/out/msvc-Release/FruityPrime.exe',
    [string]$OutputDirectory = 'tools/build/out/bomb-network',
    [ValidateRange(60, 120)][int]$Seconds = 60
)

$ErrorActionPreference = 'Stop'
$taskExe = (Resolve-Path -LiteralPath $Executable).Path
$taskGame = Split-Path $taskExe
$taskOut = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$probe = [Net.Sockets.UdpClient]::new(0)
$port = $probe.Client.LocalEndPoint.Port
$probe.Dispose()
$rotation = Join-Path $taskOut 'rotation.txt'
[IO.File]::WriteAllText($rotation, 'TEST ARENA | Battle | 30 | 1000')
$taskProcesses = @()
$names = @('BombSamus', 'BombKanden', 'BombSylux', 'BombLoss')
$hunters = @('Samus', 'Kanden', 'Sylux', 'Samus')
try {
    $server = Start-Process $taskExe -ArgumentList @('-server', '-port', $port, '-players', '4', '-rotation', ('"' + $rotation + '"'), '-nomaster', '-noautoupdate', '-noupdate') -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskOut 'server.log') -RedirectStandardError (Join-Path $taskOut 'server.err')
    $taskProcesses += $server
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($server.HasExited) { throw 'Server exited before ready' }
        $ready = Select-String -LiteralPath (Join-Path $taskOut 'server.log') -SimpleMatch 'listening on UDP' -Quiet
        if (!$ready) { Start-Sleep -Milliseconds 100 }
    } until ($ready -or [DateTime]::UtcNow -ge $deadline)
    if (!$ready) { throw 'Server did not become ready' }
    foreach ($slot in 0..3) {
        $arguments = @('-netcheck', '127.0.0.1', '-port', $port, '-name', $names[$slot], '-hunter', $hunters[$slot], '-seconds', $Seconds, '-noupdate')
        if ($slot -eq 3) { $arguments += @('-netlag', '100:20', '-netloss', '5') }
        $client = Start-Process $taskExe -ArgumentList $arguments -WorkingDirectory $taskGame -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskOut ($names[$slot] + '.log')) -RedirectStandardError (Join-Path $taskOut ($names[$slot] + '.err'))
        $null = $client.Handle
        $taskProcesses += $client
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds + 30)
    do {
        Start-Sleep -Milliseconds 200
        $pending = @($taskProcesses | Select-Object -Skip 1 | Where-Object { !$_.HasExited })
    } until (!$pending -or [DateTime]::UtcNow -ge $deadline)
    if ($pending) { throw 'Client timed out' }
    foreach ($client in $taskProcesses | Select-Object -Skip 1) {
        if ($client.ExitCode -ne 0) { throw "Client failed: exit $($client.ExitCode)" }
    }
    # Match each observer to the SAME bomb owner. Comparing this client's own
    # bombs with another player's bombs cannot prove whether either crossed.
    foreach ($owner in $names) {
        $ownerLog = Get-Content -LiteralPath (Join-Path $taskOut ($owner + '.log'))
        $own = @($ownerLog | Select-String "^  netcheck $owner mine $owner bombs (\d+)")
        if ($own.Count -ne 1) { throw "$owner has no unique bomb-owner record" }
        $ownFrames = [int]$own[0].Matches[0].Groups[1].Value
        if ($ownFrames -lt 5) { throw "$owner did not actually place bombs" }
        foreach ($observer in $names | Where-Object { $_ -ne $owner }) {
            $seen = @(Get-Content -LiteralPath (Join-Path $taskOut ($observer + '.log')) | Select-String "^  netcheck $observer saw $owner bombs (\d+)")
            if ($seen.Count -ne 1) { throw "$observer has no unique record for $owner" }
            $seenFrames = [int]$seen[0].Matches[0].Groups[1].Value
            # Arrival and local detonation timing can differ. This checks that
            # most of the owner's actual bomb lifetime was visible remotely.
            if ($seenFrames -lt 5 -or $seenFrames -lt $ownFrames * 0.6 -or $seenFrames -gt $ownFrames * 1.5) {
                throw "$observer bomb lifetime disagrees with $owner : owner=$ownFrames remote=$seenFrames"
            }
            Write-Output "BOMB NETWORK PASS observer=$observer owner=$owner ownerFrames=$ownFrames seenFrames=$seenFrames"

            # Keep the measured world-position error alongside lifetime
            # parity. Do not assign a pass threshold until a runtime baseline
            # establishes what the movement and network cadence produce.
            $position = @(Get-Content -LiteralPath (Join-Path $taskOut ($observer + '.log')) | Select-String "^  netcheck-bomb-position $observer $owner samples=(\d+) mean=([0-9.]+) max=([0-9.]+)$")
            if ($position.Count -ne 1) { throw "$observer has no unique bomb XYZ measurement for $owner" }
            $samples = [int]$position[0].Matches[0].Groups[1].Value
            $meanError = [double]::Parse($position[0].Matches[0].Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            $maxError = [double]::Parse($position[0].Matches[0].Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ($samples -le 0 -or [double]::IsNaN($meanError) -or [double]::IsInfinity($meanError) -or [double]::IsNaN($maxError) -or [double]::IsInfinity($maxError)) {
                throw "$observer has invalid bomb XYZ measurements for $owner"
            }
            Write-Output "BOMB NETWORK POSITION observer=$observer owner=$owner samples=$samples meanError=$meanError maxError=$maxError"
        }
    }
} finally {
    foreach ($process in $taskProcesses) {
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
    }
}
