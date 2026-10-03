param(
    [ValidateRange(1,1000)][int]$Cycles = 100,
    [ValidateSet('opengl','vulkan')][string]$Backend = 'opengl',
    [string]$Room = 'AD2 ALINOS PERCH',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build/out/msvc-Release'),
    [string]$OutputDirectory = "C:/tmp/rendering-stress-$(Get-Date -Format yyyyMMdd-HHmmss)"
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
$executable = Join-Path $buildPath 'FruityPrime.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw "Missing executable: $executable" }
if (Test-Path -LiteralPath $outputPath) { throw "Use a fresh capture directory: $outputPath" }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$runInfo = [ordered]@{
    sourceHead = (& git -C (Join-Path $PSScriptRoot '..') rev-parse HEAD)
    executable = $executable
    executableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    pathsSha256 = (Get-FileHash -LiteralPath (Join-Path $buildPath 'paths.txt') -Algorithm SHA256).Hash
    cycles = $Cycles
    initialBackend = $Backend
    room = $Room
    startedUtc = [DateTime]::UtcNow.ToString('o')
    complete = $false
}
$infoPath = Join-Path $outputPath 'run-info.json'
$runInfo | ConvertTo-Json | Set-Content -LiteralPath $infoPath -Encoding UTF8
$capturePath = Join-Path $outputPath 'captures'
$logPath = Join-Path $outputPath 'run.log'
$names = @('FRUITY_SWITCHSTRESS','FRUITY_SWITCHCHECK','FRUITY_SHOT_ROOM',
    'FRUITY_SWITCHCHECK_HOLD_ACTORS','FRUITY_SWITCHCHECK_WITNESS_SELFTEST','FRUITY_SWITCHCHECK_FAILURES','FRUITY_FPSCHECK')
$savedEnvironment = @{}
foreach ($name in $names) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$prefsPath = Join-Path $buildPath 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
Push-Location $buildPath
try {
    $env:FRUITY_SWITCHSTRESS = [string]$Cycles
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = $Room
    $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = '1'
    $env:FRUITY_SWITCHCHECK_WITNESS_SELFTEST = '1'
    $env:FRUITY_SWITCHCHECK_FAILURES = $null
    $env:FRUITY_FPSCHECK = $null
    'q' | & $executable -shellshot $capturePath -rhi $Backend -fpscap 240 -vkvalidation -fps off -noupdate -debuglog *> $logPath
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw "Renderer stress exited $nativeExit. See $logPath" }
    $lines = [System.IO.File]::ReadAllLines($logPath)
    if (($lines | Select-String -Pattern 'VUID|Validation Error|lifecycle FAIL|render stress.*FAIL').Count) {
        throw "Renderer stress reported a validation or lifecycle failure. See $logPath"
    }
    $expected = "[render stress] PASS; cycles=$Cycles; renderer switches=$($Cycles*2); misses=0"
    if ($lines -notcontains $expected -or $lines -notcontains '[render stress] final release PASS; live=0; retired=0; errors=0') {
        throw "Renderer stress did not reach both completion and final resource release. See $logPath"
    }
    if (@($lines | Select-String -SimpleMatch '[switch witness] PASS;').Count -ne $Cycles) {
        throw "Missing live-match switch witnesses. See $logPath"
    }
    foreach ($renderer in @('opengl','vulkan')) {
        if (@($lines | Select-String -Pattern "\[render stress\] resize cycle=\d+; backend=$renderer;.*; PASS$").Count -ne $Cycles) {
            throw "Missing $renderer resize coverage. See $logPath"
        }
    }
    for ($cycle = 1; $cycle -le $Cycles; ++$cycle) {
        $cyclePath = Join-Path $capturePath "cycle-$cycle"
        $captures = @(Get-ChildItem -LiteralPath $cyclePath -Filter '*.png' -File)
        if ($captures.Count -ne 7) { throw "Cycle $cycle has $($captures.Count) captures; expected 7." }
        foreach ($capture in $captures) {
            $stream = [System.IO.File]::OpenRead($capture.FullName)
            try {
                $header = New-Object byte[] 24
                if ($stream.Read($header,0,24) -ne 24 -or [BitConverter]::ToString($header,0,8) -ne '89-50-4E-47-0D-0A-1A-0A' -or
                    [System.Text.Encoding]::ASCII.GetString($header,12,4) -ne 'IHDR') {
                    throw "Missing PNG header: $($capture.FullName)"
                }
            } finally { $stream.Dispose() }
        }
    }
    $runInfo.complete = $true
    $runInfo.completedUtc = [DateTime]::UtcNow.ToString('o')
    $runInfo.rendererSwitches = $Cycles*2
    $runInfo.captures = $Cycles*7
    $runInfo | ConvertTo-Json | Set-Content -LiteralPath $infoPath -Encoding UTF8
    Write-Output "Renderer stress PASS: cycles=$Cycles; switches=$($Cycles*2); captures=$($Cycles*7); log=$logPath"
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath,$prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
    Pop-Location
}
