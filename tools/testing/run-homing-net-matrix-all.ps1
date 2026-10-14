<#
.SYNOPSIS
  Every cell of the homing network matrix in turn (run-homing-net-matrix.ps1),
  then one line per cell: what it was and whether every copy agreed.
#>
param(
    [int]$Seconds = 100,
    [ValidateRange(1, 3)][int]$Parallelism = 3,
    [string]$Build = "$PSScriptRoot\..\build\out\msvc-Release"
)

$cells = @(
    @{ Label = "c2-rtt0"; Clients = 2 },
    @{ Label = "c2-rtt80"; Clients = 2; LagMs = 80 },
    @{ Label = "c2-rtt150"; Clients = 2; LagMs = 150 },
    @{ Label = "c2-rtt150-jitter30"; Clients = 2; LagMs = 150; JitterMs = 30 },
    @{ Label = "c2-rtt80-loss10-reorder10"; Clients = 2; LagMs = 80; LossPercent = 10; ReorderPercent = 10 },
    @{ Label = "c8-rtt0"; Clients = 8 },
    @{ Label = "c8-rtt80"; Clients = 8; LagMs = 80 },
    @{ Label = "c8-rtt150"; Clients = 8; LagMs = 150 },
    @{ Label = "c8-rtt150-loss5-reorder5"; Clients = 8; LagMs = 150; LossPercent = 5; ReorderPercent = 5 },
    @{ Label = "c8-prime-rtt80"; Clients = 8; LagMs = 80; Mode = "PrimeHunter" },
    @{ Label = "c2-bots4-rtt80"; Clients = 2; Bots = 4; LagMs = 80 },
    @{ Label = "c2-bots4-rtt0"; Clients = 2; Bots = 4 },
    @{ Label = "c2-bots4-rtt150"; Clients = 2; Bots = 4; LagMs = 150 },
    @{ Label = "c4-shockcoil-morph-rtt80"; Clients = 4; LagMs = 80; Morph = $true; Hunters = @("Sylux", "Noxus", "Samus", "Trace") },
    @{ Label = "host-c2-rtt0"; Clients = 2; HostMode = $true },
    @{ Label = "host-c2-rtt80"; Clients = 2; HostMode = $true; LagMs = 80 },
    @{ Label = "host-c2-rtt150-loss5-reorder5"; Clients = 2; HostMode = $true; LagMs = 150; LossPercent = 5; ReorderPercent = 5 },
    @{ Label = "host-c8-prime-rtt80"; Clients = 8; HostMode = $true; LagMs = 80; Mode = "PrimeHunter" },
    @{ Label = "host-c2-server-pool-rtt80"; Clients = 2; HostMode = $true; HostService = "DedicatedServer"; LagMs = 80 }
)

# Run the role/identity regressions first, then the remaining coverage.
$priorityLabels = @('c8-rtt80', 'c8-prime-rtt80', 'host-c8-prime-rtt80')
$cells = @($priorityLabels | ForEach-Object {
    $label = $_
    $cells | Where-Object { $_.Label -eq $label }
}) + @($cells | Where-Object { $_.Label -notin $priorityLabels })
$port = 27960
$summary = @()
$failed = $false
$jobs = @()
function Collect-Cell($job) {
    $output = @(Receive-Job $job -Wait)
    Remove-Job $job
    $result = ($output | Select-String -Pattern "^RESULT " | Select-Object -Last 1).Line
    $exit = ($output | Select-String -Pattern "^CELL_EXIT_CODE:" | Select-Object -Last 1).Line
    if ($result -ne "RESULT PASS" -or $exit -ne "CELL_EXIT_CODE:0") { $script:failed = $true }
    $folder = ($output | Select-String -Pattern "^output: " | Select-Object -Last 1).Line
    $script:summary += "{0,-30} {1,-12} {2}" -f $job.Name, $result, $folder
    $output | Where-Object { $_ -notmatch '^CELL_EXIT_CODE:' } | ForEach-Object { Write-Output $_ }
}
try {
    foreach ($cell in $cells) {
        while ($jobs.Count -ge $Parallelism) {
            $done = Wait-Job -Job $jobs -Any
            Collect-Cell $done
            $jobs = @($jobs | Where-Object { $_.Id -ne $done.Id })
        }
        $arguments = @{ Seconds = $Seconds; Port = $port; Build = $Build }
        foreach ($key in $cell.Keys) { $arguments[$key] = $cell[$key] }
        $scriptPath = Join-Path $PSScriptRoot "run-homing-net-matrix.ps1"
        $jobs += Start-Job -Name $cell.Label -ScriptBlock {
            param($scriptPath, $arguments)
            & $scriptPath @arguments 2>&1
            Write-Output "CELL_EXIT_CODE:$LASTEXITCODE"
        } -ArgumentList $scriptPath, $arguments
        $port += 2
    }
    foreach ($job in $jobs) { Collect-Cell $job }
    $jobs = @()
} finally {
    foreach ($job in $jobs) {
        Stop-Job $job
        Remove-Job $job
    }
}
Write-Output "== matrix"
$summary | ForEach-Object { Write-Output $_ }
exit $(if ($failed) { 1 } else { 0 })
