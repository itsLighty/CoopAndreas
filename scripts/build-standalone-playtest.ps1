param(
    [Parameter(Mandatory = $false)]
    [string]$SannyBuilderPath = $env:SANNY_BUILDER_PATH,

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

Write-Warning 'The playtest EXE is now a permanent updater; runtime files are no longer embedded.'
& (Join-Path $PSScriptRoot 'build-playtest-updater.ps1') -OutputDirectory $OutputDirectory
