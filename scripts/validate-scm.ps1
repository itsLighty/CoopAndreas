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

$unsupportedShorthandPatterns = @(
    '^\s*[0-9]+@(?:\([^)]*\))?\s*=\s*[0-9]+@(?:\([^)]*\))?\s*(?://.*)?$',
    '^\s*[0-9]+@(?:\([^)]*\))?\s*(?:>=|>|==|<>)\s*[0-9]+@(?:\([^)]*\))?\s*$',
    '^\s*[0-9]+@(?:\([^)]*\))?\s*(?:\+=|-=|\*=|/=)\s*[0-9]+@(?:\([^)]*\))?\s*(?://.*)?$'
)
$unsupportedShorthand = @(
    Get-ChildItem -LiteralPath (Join-Path $sourceScm 'scripts') -Filter '*.txt' -File |
        Select-String -Pattern $unsupportedShorthandPatterns
)
if ($unsupportedShorthand.Count -gt 0) {
    $details = $unsupportedShorthand |
        Select-Object -First 20 |
        ForEach-Object { "$($_.Path):$($_.LineNumber) $($_.Line.Trim())" }
    throw "Unsupported local-to-local Sanny shorthand detected. Use explicit typed directives:`n$($details -join "`n")"
}

$compilerLockPath = Join-Path $sannyBuilderPath '.coopandreas-compile.lock'
$compilerLock = $null
$lockDeadline = [DateTime]::UtcNow.AddMinutes(5)
while ($null -eq $compilerLock -and [DateTime]::UtcNow -lt $lockDeadline) {
    try {
        $compilerLock = [IO.File]::Open(
            $compilerLockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None)
    }
    catch [IO.IOException] {
        Start-Sleep -Milliseconds 200
    }
}

if ($null -eq $compilerLock) {
    throw 'Timed out waiting for another Sanny Builder validation process to finish.'
}

try {
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

        $compilerLog = Join-Path $sannyBuilderPath 'compile.log'
        if (Test-Path -LiteralPath $compilerLog -PathType Leaf) {
            [System.IO.File]::Delete($compilerLog)
        }

        $compiler = Start-Process -FilePath $sannyExecutable -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
        if ($compiler.ExitCode -ne 0) {
            throw "Sanny Builder exited with code $($compiler.ExitCode)."
        }

        if (Test-Path -LiteralPath $compilerLog -PathType Leaf) {
            $compilerDiagnostic = Get-Content -LiteralPath $compilerLog -Raw
            if ($compilerDiagnostic -match '(?im)^\s*(?:error|fatal):') {
                throw "Sanny Builder reported a compiler diagnostic:`n$($compilerDiagnostic.Trim())"
            }
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
}
finally {
    $compilerLock.Dispose()
}
