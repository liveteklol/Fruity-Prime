<#
.SYNOPSIS
  Start a playable native session with AI clients on delayed UDP connections.
.EXAMPLE
  tools\test-session\start.bat
  tools\test-session\start.bat -Bots 3 -BotPingMs 250 -Hunter Kanden
  tools\test-session\stop.bat
#>
param(
    [string]$Build = "$PSScriptRoot\..\..\build\out\msvc-Release",
    [ValidateSet('Samus','Kanden','Spire','Noxus','Trace','Weavel','Sylux')][string]$Hunter = 'Kanden',
    [ValidateRange(0,7)][int]$Bots = 7,
    [ValidateRange(0,5000)][int]$BotPingMs = 250,
    [string]$Room = 'MP1 SANCTORUS',
    [ValidateRange(1024,65535)][int]$Port = 27888,
    [ValidateRange(60,86400)][int]$Seconds = 7200,
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$stateFile = Join-Path $repo 'tools\build\out\test-session.json'

function Get-OwnedProcess($Entry) {
    $process = Get-Process -Id $Entry.Id -ErrorAction SilentlyContinue
    if ($process -and $process.Path -eq $Entry.Exe -and
        $process.StartTime.ToUniversalTime().Ticks -eq ([datetime]$Entry.StartTime).ToUniversalTime().Ticks) {
        return $process
    }
}
function Stop-OwnedProcesses($Entries) {
    foreach ($entry in $Entries) {
        $process = Get-OwnedProcess $entry
        if ($process) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    }
}
if (Test-Path -LiteralPath $stateFile) {
    $previous = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    $alive = @($previous.Processes | ForEach-Object { Get-OwnedProcess $_ })
    if ($Stop) {
        Stop-OwnedProcesses $previous.Processes
        Write-Output 'Test session stopped.'
        exit 0
    }
    if ($alive.Count -gt 0) { throw 'A test session is already running. Use tools\test-session\stop.bat first.' }
} elseif ($Stop) {
    Write-Output 'No test session is running.'
    exit 0
}

$buildDir = (Resolve-Path -LiteralPath $Build).Path
$exe = Join-Path $buildDir 'FruityPrime.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the native game first: tools\build\build-cpp.bat msvc Release' }
if ($Room -match '[|\r\n]') { throw 'Room must be a single map name.' }
while ($true) {
    $probe = [System.Net.Sockets.UdpClient]::new()
    try { $probe.Client.Bind([System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any,$Port)); break }
    catch { if ($Port -eq 65535) { throw }; $Port++ }
    finally { $probe.Dispose() }
}
$sessionDir = Join-Path $buildDir ('manual-tests\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null
$rotation = Join-Path $sessionDir 'rotation.txt'
[System.IO.File]::WriteAllText($rotation, "$Room | Battle | 60 | 999`n", [System.Text.UTF8Encoding]::new($false))
$script:sessionProcesses = @()

function Quote-NativeArgument([string]$Value) {
    # Windows CRT quoting, including embedded quotes and trailing backslashes.
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Start-SessionProcess([string]$Role, [string[]]$Arguments, [switch]$Visible) {
    $env:FRUITY_DEBUG_LOG_DIRECTORY = Join-Path $sessionDir "$Role-logs"
    $quoted = @($Arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $style = if ($Visible) { 'Normal' } else { 'Hidden' }
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $buildDir -PassThru -WindowStyle $style `
        -RedirectStandardOutput (Join-Path $sessionDir "$Role.stdout.txt") -RedirectStandardError (Join-Path $sessionDir "$Role.stderr.txt")
    $null = $process.Handle
    $script:sessionProcesses += [pscustomobject]@{Role=$Role;Id=$process.Id;StartTime=$process.StartTime.ToUniversalTime().ToString('o');Exe=$exe}
    return $process
}
function Wait-Server($Process) {
    $deadline = (Get-Date).AddSeconds(30)
    $udp = [System.Net.Sockets.UdpClient]::new()
    try {
        $udp.Connect('127.0.0.1',$Port)
        $udp.Client.ReceiveTimeout = 500
        while ((Get-Date) -lt $deadline) {
            if ($Process.HasExited) { throw "Server stopped. See $sessionDir\server.stdout.txt" }
            try {
                # Directory StatusQuery/StatusReply; no room or player is created.
                $null = $udp.Send([byte[]]@(14),1)
                $sender = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any,0)
                $reply = $udp.Receive([ref]$sender)
                if ($reply.Length -gt 1 -and $reply[0] -eq 15) { return }
            } catch [System.Net.Sockets.SocketException] { }
            Start-Sleep -Milliseconds 100
        }
        throw "Server did not become ready. See $sessionDir\server.stdout.txt"
    } finally { $udp.Dispose() }
}
function Wait-Joined($Process, [string]$Role, [string]$Pattern) {
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) { throw "$Role stopped. See $sessionDir\$Role.stdout.txt" }
        $output = Get-Content -LiteralPath (Join-Path $sessionDir "$Role.stdout.txt") -Raw -ErrorAction SilentlyContinue
        if ($output -match $Pattern) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "$Role did not join. See $sessionDir\$Role.stdout.txt"
}

$savedPath = $env:PATH
$savedLog = $env:FRUITY_DEBUG_LOG_DIRECTORY
try {
    # Use the selected build's Qt runtime rather than a machine-specific version.
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $qt = Get-Content -LiteralPath $cache | Select-String '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
        if ($qt) {
            $qtBin = [System.IO.Path]::GetFullPath((Join-Path $qt.Matches[0].Groups[1].Value '..\..\..\bin'))
            if (Test-Path -LiteralPath $qtBin) { $env:PATH = $qtBin + ';' + $savedPath }
        }
    }
    $common = @('-homingtrace','-debuglog','-noupdate','-noautoupdate')
    $server = Start-SessionProcess 'server' (@('-server','-port',"$Port",'-players','8','-rotation',$rotation,'-nomaster','-servername',"ManualBots$BotPingMs") + $common)
    Wait-Server $server
    $hunters = @('Samus','Spire','Noxus','Trace','Weavel','Sylux','Kanden')
    for ($i=0; $i -lt $Bots; $i++) {
        $role = "bot$i"
        $bot = Start-SessionProcess $role (@('-netcheck','127.0.0.1','-port',"$Port",'-name',"Bot$i$($hunters[$i])",'-hunter',$hunters[$i],'-ai','2','-headless','-seconds',"$Seconds",'-netlag',"$BotPingMs") + $common)
        Wait-Joined $bot $role 'connected in .*slot'
    }
    $player = Start-SessionProcess 'player' (@('-connect','127.0.0.1','-port',"$Port",'-name','Zection','-hunter',$Hunter) + $common) -Visible
    Wait-Joined $player 'player' 'slot \d+ activated \(Zection\)'
    Write-Output "Ready: $Hunter + $Bots bots (AI level 2, added RTT $BotPingMs ms)."
    Write-Output "Connect: 127.0.0.1:$Port -- $Room"
    Write-Output "Logs: $sessionDir"
    Write-Output 'Stop all session processes: tools\test-session\stop.bat'
} catch {
    Stop-OwnedProcesses $script:sessionProcesses
    throw
} finally {
    $env:PATH = $savedPath
    $env:FRUITY_DEBUG_LOG_DIRECTORY = $savedLog
    $state = [pscustomobject]@{SessionDirectory=$sessionDir;Port=$Port;Hunter=$Hunter;Bots=$Bots;BotPingMs=$BotPingMs;Processes=$script:sessionProcesses}
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionDir 'processes.json') -Encoding utf8
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $stateFile -Encoding utf8
}
