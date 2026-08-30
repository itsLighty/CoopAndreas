param(
    [Parameter(Mandatory = $false)]
    [string]$SannyBuilderPath = $env:SANNY_BUILDER_PATH,

    [switch]$KeepTemp
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceScm = Join-Path $repositoryRoot 'scm'
$customModeSource = Join-Path $repositoryRoot 'sdk\Sanny Builder 4\data\sa_sbl_coopandreas'

if ([string]::IsNullOrWhiteSpace($SannyBuilderPath)) {
    throw 'Pass -SannyBuilderPath or set SANNY_BUILDER_PATH to a Sanny Builder 4 directory.'
}

$sannyBuilderPath = (Resolve-Path -LiteralPath $SannyBuilderPath).Path
$sannyExecutable = Join-Path $sannyBuilderPath 'sanny.exe'
if (-not (Test-Path -LiteralPath $sannyExecutable -PathType Leaf)) {
    throw "Sanny Builder executable not found at '$sannyExecutable'."
}

$customModeDestination = Join-Path $sannyBuilderPath 'data\sa_sbl_coopandreas'
New-Item -ItemType Directory -Path $customModeDestination -Force | Out-Null
Copy-Item -Path (Join-Path $customModeSource '*') -Destination $customModeDestination -Recurse -Force

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('coopandreas-scm-check-' + [guid]::NewGuid().ToString('N'))
$temporaryScm = Join-Path $temporaryRoot 'scm'
$compiledScm = Join-Path $temporaryScm 'compiled-main.scm'

try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    Copy-Item -LiteralPath $sourceScm -Destination $temporaryScm -Recurse

    $inputScm = Join-Path $temporaryScm 'main.txt'
    $arguments = @(
        '--no-splash',
        '--mode',
        'sa_sbl_coopandreas',
        '--compile',
        ('"' + $inputScm + '"'),
        ('"' + $compiledScm + '"')
    )

    $compiler = Start-Process -FilePath $sannyExecutable -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
    if ($compiler.ExitCode -ne 0) {
        throw "Sanny Builder exited with code $($compiler.ExitCode)."
    }

    if (-not (Test-Path -LiteralPath $compiledScm -PathType Leaf)) {
        throw 'Sanny Builder reported success but did not create the compiled SCM.'
    }

    $compiledFile = Get-Item -LiteralPath $compiledScm
    if ($compiledFile.Length -eq 0) {
        throw 'Sanny Builder created an empty compiled SCM.'
    }

    Write-Output "SCM compilation succeeded ($($compiledFile.Length) bytes)."
    if ($KeepTemp) {
        Write-Output "Validation artifacts: $temporaryRoot"
    }
}
finally {
    if (-not $KeepTemp -and (Test-Path -LiteralPath $temporaryRoot)) {
        $resolvedTemporaryRoot = (Resolve-Path -LiteralPath $temporaryRoot).Path
        $systemTemporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
        $temporaryLeaf = Split-Path -Leaf $resolvedTemporaryRoot
        if (-not $resolvedTemporaryRoot.StartsWith($systemTemporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
            -not $temporaryLeaf.StartsWith('coopandreas-scm-check-', [System.StringComparison]::Ordinal)) {
            throw "Refusing to remove unexpected validation directory '$resolvedTemporaryRoot'."
        }

        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}
