param(
    [Parameter(Mandatory = $false)]
    [string]$SannyBuilderPath = $env:SANNY_BUILDER_PATH,

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $false)]
    [string]$CommitSha
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
$safeBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build'))

function Reset-BuildDirectory([string]$Path) {
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($safeBuildRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset playtest directory outside '$safeBuildRoot': $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null
}

if ([string]::IsNullOrWhiteSpace($CommitSha)) {
    $CommitSha = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not determine the commit to identify the playtest package.'
    }
}
$CommitSha = $CommitSha.Trim().ToLowerInvariant()
if ($CommitSha -notmatch '^[0-9a-f]{40}$') {
    throw "CommitSha must be a full 40-character Git commit hash, got '$CommitSha'."
}

Reset-BuildDirectory $releaseAssets
Reset-BuildDirectory $packageRoot
New-Item -ItemType Directory -Path $packageCoopData -Force | Out-Null

$runtimeFiles = @(
    @{ Source = 'CoopAndreasSA.dll'; Destination = 'CoopAndreasSA.dll' },
    @{ Source = 'proxy.dll'; Destination = 'eax.dll' },
    @{ Source = 'LaunchCoopAndreas.exe'; Destination = 'LaunchCoopAndreas.exe' },
    @{ Source = 'LaunchCoopAndreas.exe.manifest'; Destination = 'LaunchCoopAndreas.exe.manifest' },
    @{ Source = 'server.exe'; Destination = 'server.exe' }
)

foreach ($runtimeFile in $runtimeFiles) {
    $source = Join-Path $windowsBuild $runtimeFile.Source
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required Windows build artifact is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $packageRoot $runtimeFile.Destination) -Force
}

$updater = Join-Path $windowsBuild 'CoopAndreasPlaytest.exe'
if (-not (Test-Path -LiteralPath $updater -PathType Leaf)) {
    throw "Required permanent updater is missing: $updater"
}
Copy-Item -LiteralPath $updater -Destination (Join-Path $releaseAssets 'CoopAndreasPlaytest.exe') -Force

& (Join-Path $PSScriptRoot 'validate-scm.ps1') `
    -SannyBuilderPath $SannyBuilderPath `
    -OutputDirectory $packageCoopData

@(
    "commit=$CommitSha"
    "source=https://github.com/itsLighty/CoopAndreas/commit/$CommitSha"
) | Set-Content -LiteralPath (Join-Path $packageCoopData 'playtest-build.txt') -Encoding ascii

$packageName = "CoopAndreas-playtest-$CommitSha.zip"
$packagePath = Join-Path $releaseAssets $packageName
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $packagePath -CompressionLevel Optimal -Force

$packageFile = Get-Item -LiteralPath $packagePath
$packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
@(
    'format=1'
    "commit=$CommitSha"
    "package=$packageName"
    "sha256=$packageHash"
    "size=$($packageFile.Length)"
) | Set-Content -LiteralPath (Join-Path $releaseAssets 'playtest-manifest.txt') -Encoding ascii

Write-Output "Playtest package: $packagePath"
Write-Output "Playtest package SHA256: $packageHash"
Write-Output "Rolling manifest: $(Join-Path $releaseAssets 'playtest-manifest.txt')"
