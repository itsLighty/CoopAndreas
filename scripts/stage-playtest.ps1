param(
    [Parameter(Mandatory = $false)]
    [string]$SannyBuilderPath = $env:SANNY_BUILDER_PATH,

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot 'build\playtest'
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot $OutputDirectory
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$releaseAssets = Join-Path $outputRoot 'release-assets'
$packageRoot = Join-Path $outputRoot 'package'
$packageCoopData = Join-Path $packageRoot 'CoopAndreas'
$windowsBuild = Join-Path $repositoryRoot 'build\windows\x86\release'

New-Item -ItemType Directory -Path $releaseAssets -Force | Out-Null
New-Item -ItemType Directory -Path $packageCoopData -Force | Out-Null

$runtimeFiles = @(
    @{ Source = 'CoopAndreasSA.dll'; Destination = 'CoopAndreasSA.dll' },
    @{ Source = 'proxy.dll'; Destination = 'eax.dll' },
    @{ Source = 'LaunchCoopAndreas.exe'; Destination = 'LaunchCoopAndreas.exe' },
    @{ Source = 'LaunchCoopAndreas.exe.manifest'; Destination = 'LaunchCoopAndreas.exe.manifest' },
    @{ Source = 'server.exe'; Destination = 'server.exe' },
    @{ Source = 'CoopAndreasPlaytest.exe'; Destination = 'CoopAndreasPlaytest.exe' }
)

foreach ($runtimeFile in $runtimeFiles) {
    $source = Join-Path $windowsBuild $runtimeFile.Source
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required Windows build artifact is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $releaseAssets $runtimeFile.Destination) -Force
    Copy-Item -LiteralPath $source -Destination (Join-Path $packageRoot $runtimeFile.Destination) -Force
}

& (Join-Path $PSScriptRoot 'validate-scm.ps1') `
    -SannyBuilderPath $SannyBuilderPath `
    -OutputDirectory $releaseAssets

Copy-Item -LiteralPath (Join-Path $releaseAssets 'main.scm') -Destination (Join-Path $packageCoopData 'main.scm') -Force
Copy-Item -LiteralPath (Join-Path $releaseAssets 'script.img') -Destination (Join-Path $packageCoopData 'script.img') -Force

$archivePath = Join-Path $releaseAssets 'CoopAndreas-playtest.zip'
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $archivePath -CompressionLevel Optimal -Force

$hashLines = Get-ChildItem -LiteralPath $releaseAssets -File |
    Where-Object Name -ne 'SHA256SUMS.txt' |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $($_.Name)"
    }
$hashLines | Set-Content -LiteralPath (Join-Path $releaseAssets 'SHA256SUMS.txt') -Encoding ascii

Write-Output "Playtest release assets staged at $releaseAssets"
