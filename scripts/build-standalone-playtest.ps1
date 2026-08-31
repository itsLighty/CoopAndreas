param(
    [Parameter(Mandatory = $false)]
    [string]$SannyBuilderPath = $env:SANNY_BUILDER_PATH,

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot 'build\playtest\standalone'
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot $OutputDirectory
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$assetRoot = Join-Path $repositoryRoot 'build\playtest\standalone-assets'
$windowsBuild = Join-Path $repositoryRoot 'build\windows\x86\release'

$xmakeCommand = Get-Command xmake -ErrorAction SilentlyContinue
if ($xmakeCommand) {
    $xmake = $xmakeCommand.Source
}
else {
    $xmake = 'C:\Program Files\xmake\xmake.exe'
}
if (-not (Test-Path -LiteralPath $xmake -PathType Leaf)) {
    throw 'Xmake was not found. Install it or add it to PATH.'
}

Push-Location $repositoryRoot
try {
    & $xmake build client server proxy launcher
    if ($LASTEXITCODE -ne 0) {
        throw "The CoopAndreas runtime build failed with exit code $LASTEXITCODE."
    }

    & (Join-Path $PSScriptRoot 'validate-scm.ps1') `
        -SannyBuilderPath $SannyBuilderPath `
        -OutputDirectory $assetRoot

    & $xmake build -r playtest_launcher
    if ($LASTEXITCODE -ne 0) {
        throw "The standalone playtest build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$sourceExecutable = Join-Path $windowsBuild 'CoopAndreasPlaytest.exe'
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "The standalone executable was not produced at '$sourceExecutable'."
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$outputExecutable = Join-Path $outputRoot 'CoopAndreasPlaytest.exe'
Copy-Item -LiteralPath $sourceExecutable -Destination $outputExecutable -Force

$file = Get-Item -LiteralPath $outputExecutable
$hash = (Get-FileHash -LiteralPath $outputExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Standalone playtest: $($file.FullName)"
Write-Output "Size: $($file.Length) bytes"
Write-Output "SHA256: $hash"
