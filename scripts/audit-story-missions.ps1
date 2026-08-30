[CmdletBinding()]
param(
    [switch]$RequireReady,
    [switch]$SummaryOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-ReferencedCoopLabels {
    param(
        [Parameter(Mandatory)]
        [string]$Script,

        [Parameter(Mandatory)]
        [string]$PurposePattern
    )

    $labels = [regex]::Matches(
        $Script,
        "(?im)^:(?<label>[A-Z0-9_]*COOP[A-Z0-9_]*$PurposePattern[A-Z0-9_]*)\s*$"
    )

    foreach ($labelMatch in $labels) {
        $label = $labelMatch.Groups['label'].Value
        $escapedLabel = [regex]::Escape($label)
        if ([regex]::IsMatch($Script, "(?im)^\s*(?:gosub|goto)\s+@$escapedLabel\s*$")) {
            $label
        }
    }
}

function Get-UnboundedNetworkIdWaitLines {
    param(
        [Parameter(Mandatory)]
        [string]$Script
    )

    $lines = [regex]::Split($Script, '\r?\n')
    $unboundedLines = [System.Collections.Generic.List[int]]::new()

    for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex) {
        $whileMatch = [regex]::Match(
            $lines[$lineIndex],
            '^(?<indent>\s*)while\s+[^\r\n]+==\s*-1\s*(?://.*)?$',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
        )
        if (-not $whileMatch.Success) {
            continue
        }

        $indent = [regex]::Escape($whileMatch.Groups['indent'].Value)
        $bodyLines = [System.Collections.Generic.List[string]]::new()
        for ($bodyIndex = $lineIndex + 1; $bodyIndex -lt $lines.Count; ++$bodyIndex) {
            if ($lines[$bodyIndex] -match "^${indent}end\s*(?://.*)?$") {
                break
            }
            $bodyLines.Add($lines[$bodyIndex])
        }

        $body = $bodyLines -join "`n"
        $hasClock = $body -match '(?i)(?:Clock\.GetGameTimer\s*\(|\bTIMER[ABC]\b)'
        $hasDeadlineComparison = $body -match '(?i)(?:>=|>|greater(?:_or_equal)?_to)'
        $hasExit = $body -match '(?im)^\s*(?:break|return|goto(?:_if_false)?\s+@)'
        if (-not ($hasClock -and $hasDeadlineComparison -and $hasExit)) {
            $unboundedLines.Add($lineIndex + 1)
        }
    }

    return $unboundedLines.ToArray()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$missionRunnerPath = Join-Path $repoRoot 'client/src/Debug/MissionRunner.cpp'
$mainScmPath = Join-Path $repoRoot 'scm/main.txt'

$missionRunner = Get-Content -LiteralPath $missionRunnerPath -Raw
$mainScm = Get-Content -LiteralPath $mainScmPath -Raw

$runnerPattern = '\{"(?<name>[^"]+)",\s*(?<id>\d+)\}'
$definitionPattern = '(?m)^DEFINE MISSION\s+(?<id>\d+)\s+AT\s+@(?<script>[A-Z0-9_]+)\s*//\s*(?<name>.+?)\s*$'

$definitions = @{}
foreach ($match in [regex]::Matches($mainScm, $definitionPattern)) {
    $missionId = [int]$match.Groups['id'].Value
    if ($definitions.ContainsKey($missionId)) {
        throw "Duplicate SCM mission definition for ID $missionId."
    }

    $definitions[$missionId] = [pscustomobject]@{
        Script = $match.Groups['script'].Value
        Name = $match.Groups['name'].Value.Trim()
    }
}

$seenRunnerIds = [System.Collections.Generic.HashSet[int]]::new()
$seenRunnerNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$seenRunnerScripts = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

$rows = foreach ($match in [regex]::Matches($missionRunner, $runnerPattern)) {
    $missionId = [int]$match.Groups['id'].Value
    $runnerName = $match.Groups['name'].Value
    if (-not $seenRunnerIds.Add($missionId)) {
        throw "MissionRunner contains duplicate mission ID $missionId."
    }
    if (-not $seenRunnerNames.Add($runnerName)) {
        throw "MissionRunner contains duplicate mission name '$runnerName'."
    }
    if (-not $definitions.ContainsKey($missionId)) {
        throw "MissionRunner entry '$runnerName' references undefined mission ID $missionId."
    }

    $definition = $definitions[$missionId]
    if (-not $seenRunnerScripts.Add($definition.Script)) {
        throw "MissionRunner maps more than one entry to script '$($definition.Script)'."
    }

    $scriptPath = Join-Path $repoRoot "scm/scripts/$($definition.Script).txt"
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Mission $missionId ('$runnerName') maps to missing script '$scriptPath'."
    }

    $script = Get-Content -LiteralPath $scriptPath -Raw
    $syncHook = $script -match 'Coop\.EnableSyncingThisScript\s*\('
    $rosterCollectionCount = ([regex]::Matches($script, 'Coop\.CollectNetworkPlayersForTheMission\s*\(')).Count
    $rosterHook = $rosterCollectionCount -gt 0
    $rosterLabels = @(Get-ReferencedCoopLabels -Script $script -PurposePattern '(?:(?:UPDATE|REFRESH|VALIDATE)[A-Z0-9_]*ROSTER|ROSTER[A-Z0-9_]*(?:UPDATE|REFRESH|VALIDATE)|RECONNECT)')
    $periodicRecollection = $rosterCollectionCount -ge 2 -and $rosterLabels.Count -gt 0

    $internalIdCount = ([regex]::Matches($script, 'Coop\.GetNetworkPlayerInternalId\s*\(')).Count
    $identityComparison = $script -match '(?i)(?:is_int_[a-z_]*equal_to_int_[a-z_]*|==)'
    $immutableIdentity = $internalIdCount -ge 2 -and $identityComparison
    $validityGuards = ([regex]::Matches($script, 'Coop\.IsNetworkPlayerActorValid\s*\(')).Count
    $reconnectSafe = $periodicRecollection -and $immutableIdentity -and $validityGuards -gt 0

    # Static evidence is deliberately explicit: a co-op participant-death marker/check plus an actor-death
    # predicate and a peer failure result. This avoids mistaking stock escort/enemy death checks for policy.
    $participantDeathMarker = $script -match '(?i)(?:COOP[A-Z0-9_]*PARTICIPANT[A-Z0-9_]*DEATH|deterministic[^\r\n]{0,120}(?:participant|cooperative)[^\r\n]{0,80}(?:death|fail))'
    $actorDeathPredicate = $script -match '(?i)Char\.IsDead\s*\('
    $peerFailureResult = $script -match "(?is)Coop\.(?:PrintBig|PrintNow|PrintHigh|PrintLow|ShowText)[A-Za-z]*ForNetworkPlayer\s*\([^\)]*'M_FAIL'"
    $participantDeathPolicy = $participantDeathMarker -and $actorDeathPredicate -and $peerFailureResult

    $networkIdHandshakeCount = ([regex]::Matches($script, 'Coop\.Get(?:Ped|Vehicle)NetworkId\s*\(')).Count
    $networkIdWaitLoopCount = ([regex]::Matches($script, '(?im)^\s*while\s+[^\r\n]+==\s*-1\s*(?://.*)?$')).Count
    $unboundedWaitLines = @(Get-UnboundedNetworkIdWaitLines -Script $script)
    $boundedRegistration = $unboundedWaitLines.Count -eq 0

    # Result fanout labels in the converted scripts use RESULT/OUTCOME as well as explicit
    # NOTIFY_PASS, NOTIFY_FAILURE, PASS_REMOTES, and FAIL_REMOTES forms. Keep the check tied
    # to a referenced co-op label while accepting those semantically equivalent names.
    $resultLabels = @(Get-ReferencedCoopLabels -Script $script -PurposePattern '(?:(?:NOTIFY|BROADCAST|FANOUT|SEND)[A-Z0-9_]*(?:RESULT|OUTCOME|PASS|FAIL(?:URE)?)|(?:RESULT|OUTCOME|PASS|FAIL(?:URE)?)[A-Z0-9_]*(?:NOTIFY|BROADCAST|FANOUT|SEND|REMOTES)|RESULT|OUTCOME)')
    $peerPassResult = $script -match "(?is)Coop\.(?:PrintBig|PrintNow|PrintHigh|PrintLow|ShowText)[A-Za-z]*ForNetworkPlayer\s*\([^\)]*'M_PASS[A-Z0-9_]*'"
    # Multi-part finale segments intentionally transition without showing an intermediate
    # pass card. They must opt in explicitly so an omitted pass fanout cannot pass silently.
    $continuedResult = $script -match '(?im)\bCOOP_RESULT_CONTINUES\b'
    $resultFanout = $resultLabels.Count -gt 0 -and $peerFailureResult -and ($peerPassResult -or $continuedResult)

    $cleanupLabels = @(Get-ReferencedCoopLabels -Script $script -PurposePattern 'CLEANUP')
    $idempotentCleanup = $cleanupLabels.Count -gt 0 -and
        $script -match '(?ims)^:[A-Z0-9_]*COOP[A-Z0-9_]*CLEANUP[A-Z0-9_]*\s*\r?\n\s*if[\s\S]{0,180}?==\s*1[\s\S]{0,100}?return'

    $unsupportedWarning = $script -match '(?i)currently unsupported|not recommended to play|may cause desyncs'
    $readySignal = $syncHook -and $rosterHook -and $reconnectSafe -and $participantDeathPolicy -and
        $boundedRegistration -and $resultFanout -and $idempotentCleanup -and -not $unsupportedWarning

    $issues = [System.Collections.Generic.List[string]]::new()
    if (-not $syncHook) { $issues.Add('sync') }
    if (-not $rosterHook) { $issues.Add('roster') }
    if (-not $periodicRecollection) { $issues.Add('recollection') }
    if (-not $immutableIdentity) { $issues.Add('identity') }
    if (-not $reconnectSafe) { $issues.Add('reconnect') }
    if (-not $participantDeathPolicy) { $issues.Add('death-policy') }
    if (-not $boundedRegistration) { $issues.Add("unbounded-id-wait:$($unboundedWaitLines -join ',')") }
    if (-not $resultFanout) { $issues.Add('result-fanout') }
    if (-not $idempotentCleanup) { $issues.Add('cleanup') }
    if ($unsupportedWarning) { $issues.Add('warning') }

    [pscustomobject]@{
        Id = $missionId
        Mission = $runnerName
        Script = $definition.Script
        Sync = $syncHook
        Roster = $rosterHook
        Recollect = $periodicRecollection
        Identity = $immutableIdentity
        Reconnect = $reconnectSafe
        DeathPolicy = $participantDeathPolicy
        BoundedIds = $boundedRegistration
        Results = $resultFanout
        Cleanup = $idempotentCleanup
        EntityHandshakes = $networkIdHandshakeCount
        IdWaitLoops = $networkIdWaitLoopCount
        Warning = $unsupportedWarning
        ReadySignal = $readySignal
        Issues = $issues -join ','
    }
}

if ($rows.Count -eq 0) {
    throw 'No MissionRunner entries were found.'
}

if (-not $SummaryOnly) {
    $rows | Sort-Object Id
}

$readyCount = @($rows | Where-Object ReadySignal).Count
$pendingRows = @($rows | Where-Object { -not $_.ReadySignal })
Write-Host "Story mission runner entries: $($rows.Count); protocol-ready: $readyCount; pending: $($pendingRows.Count)."

if ($pendingRows.Count -gt 0) {
    $pendingSummary = $pendingRows | ForEach-Object { "$($_.Id):$($_.Script)[$($_.Issues)]" }
    Write-Host "Pending protocol evidence: $($pendingSummary -join '; ')"
}

if ($RequireReady -and $pendingRows.Count -ne 0) {
    throw "$($pendingRows.Count) story mission runner entries still lack complete co-op protocol evidence."
}
