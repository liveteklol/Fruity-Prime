param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string]$Room
)

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$workingDirectory = Split-Path -Parent $resolvedExecutable
$logDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    'FruityPrime.ItemPickupCheck.' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $logDirectory | Out-Null

function Invoke-LoggedProcess {
    param(
        [string[]]$Arguments,
        [string]$Name
    )

    $stdout = Join-Path $logDirectory ($Name + '.stdout.log')
    $stderr = Join-Path $logDirectory ($Name + '.stderr.log')
    $process = Start-Process -FilePath $resolvedExecutable `
        -ArgumentList $Arguments `
        -WorkingDirectory $workingDirectory `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru -Wait -WindowStyle Hidden

    [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Stdout = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
        Stderr = if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw } else { '' }
    }
}

$roomResult = Invoke-LoggedProcess @('-console', '-rooms', '-noupdate') 'rooms'
if ($roomResult.ExitCode -ne 0) {
    throw "Could not read multiplayer rooms (exit $($roomResult.ExitCode)). Logs: $logDirectory"
}

$rooms = @($roomResult.Stdout -split '\r?\n' |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and $_ -notmatch '^\[' })
if ($rooms.Count -eq 0) {
    throw "The executable returned no multiplayer rooms. Logs: $logDirectory"
}

if ($Room) {
    $selectedRoom = @($rooms | Where-Object { $_ -eq $Room })
    if ($selectedRoom.Count -eq 0) {
        throw "Room '$Room' is not in the multiplayer room list. Logs: $logDirectory"
    }
    $rooms = $selectedRoom
}

$failedRooms = [System.Collections.Generic.List[string]]::new()
foreach ($currentRoom in $rooms) {
    Write-Host "Checking pickup collection in $currentRoom"
    $safeName = [System.Text.RegularExpressions.Regex]::Replace($currentRoom, '[^A-Za-z0-9._-]', '_')
    $quotedRoom = '"' + $currentRoom.Replace('"', '\"') + '"'
    $result = Invoke-LoggedProcess @(
        '-console', '-maptest', $quotedRoom,
        '-players', '1', '-hunter', 'Samus', '-itempickupcheck', '-noupdate') $safeName

    $summary = @($result.Stdout -split '\r?\n' |
        Where-Object { $_ -match '^ITEMPICKUPSWEEP ' } |
        Select-Object -Last 1)
    if ($result.ExitCode -eq 0 -and $summary.Count -eq 1 -and $summary[0] -match '\|\s+\d+ pickup\(s\) tested\s+\|\s+0 failed$') {
        Write-Host $summary[0]
        continue
    }

    $failedRooms.Add($currentRoom)
    Write-Error "Pickup check failed in '$currentRoom' (exit $($result.ExitCode))."
    @($result.Stdout, $result.Stderr) -split '\r?\n' |
        Where-Object { $_ -match '^(ITEMPICKUP FAIL|ITEMPICKUPSWEEP|MAPCRASH|ERROR|Exception)' } |
        ForEach-Object { Write-Host $_ }
}

if ($failedRooms.Count -gt 0) {
    Write-Error ("Pickup checks failed in: " + ($failedRooms -join ', '))
    Write-Error "Per-map logs: $logDirectory"
    exit 1
}

Remove-Item -LiteralPath $logDirectory -Recurse -Force
Write-Host "Pickup collection passed in all $($rooms.Count) map(s)."
