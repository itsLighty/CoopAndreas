[CmdletBinding()]
param(
    [switch]$RequireReady,
    [switch]$SummaryOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$missionRunnerPath = Join-Path $repoRoot 'client/src/Debug/MissionRunner.cpp'
$mainScmPath = Join-Path $repoRoot 'scm/main.txt'

$missionRunner = Get-Content -LiteralPath $missionRunnerPath -Raw
$mainScm = Get-Content -LiteralPath $mainScmPath -Raw

$runnerPattern = '\{"(?<name>[^"]+)",\s*(?<id>\d+)\}'
$definitionPattern = '(?m)^DEFINE MISSION\s+(?<id>\d+)\s+AT\s+@(?<script>[A-Z0-9_]+)\s*//\s*(?<name>.+?)\s*$'

$definitions = @{}
foreach ($match in [regex]::Matches($mainScm, $definitionPattern)) {
    $definitions[[int]$match.Groups['id'].Value] = [pscustomobject]@{
        Script = $match.Groups['script'].Value
        Name = $match.Groups['name'].Value.Trim()
    }
}

$rows = foreach ($match in [regex]::Matches($missionRunner, $runnerPattern)) {
    $missionId = [int]$match.Groups['id'].Value
    $runnerName = $match.Groups['name'].Value
    if (-not $definitions.ContainsKey($missionId)) {
        throw "MissionRunner entry '$runnerName' references undefined mission ID $missionId."
    }

    $definition = $definitions[$missionId]
    $scriptPath = Join-Path $repoRoot "scm/scripts/$($definition.Script).txt"
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Mission $missionId ('$runnerName') maps to missing script '$scriptPath'."
    }

    $script = Get-Content -LiteralPath $scriptPath -Raw
    $syncHook = $script -match 'Coop\.EnableSyncingThisScript\s*\('
    $rosterHook = $script -match 'Coop\.CollectNetworkPlayersForTheMission\s*\('
    $unsupportedWarning = $script -match '(?i)currently unsupported|not recommended to play|may cause desyncs'
    $networkIdHandshakeCount = ([regex]::Matches($script, 'Coop\.Get(?:Ped|Vehicle)NetworkId\s*\(')).Count
    $networkIdWaitLoopCount = ([regex]::Matches($script, '(?m)^\s*while\s+[^\r\n]+==\s*-1\s*$')).Count
    $readySignal = $syncHook -and $rosterHook -and -not $unsupportedWarning

    [pscustomobject]@{
        Id = $missionId
        Mission = $runnerName
        Script = $definition.Script
        Sync = $syncHook
        Roster = $rosterHook
        EntityHandshakes = $networkIdHandshakeCount
        IdWaitLoops = $networkIdWaitLoopCount
        Warning = $unsupportedWarning
        ReadySignal = $readySignal
    }
}

if ($rows.Count -eq 0) {
    throw 'No MissionRunner entries were found.'
}

if (-not $SummaryOnly) {
    $rows | Sort-Object Id
}

$readyCount = @($rows | Where-Object ReadySignal).Count
$pendingCount = $rows.Count - $readyCount
Write-Host "Story mission runner entries: $($rows.Count); readiness signals present: $readyCount; pending: $pendingCount."

if ($RequireReady -and $pendingCount -ne 0) {
    throw "$pendingCount story mission runner entries still lack complete co-op readiness signals."
}
