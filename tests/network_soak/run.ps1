param(
    [int]$Port = 6777,
    [int]$Cycles = 20,
    [int]$DurationMs = 60000
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$serverBinary = Join-Path $repositoryRoot "build\windows\x86\release\server.exe"
$soakBinary = Join-Path $repositoryRoot "build\windows\x86\release\network_soak.exe"

& xmake build -y server network_soak
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

$runDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("CoopAndreas-network-soak-" + [Guid]::NewGuid())
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$stagedServer = Join-Path $runDirectory "server.exe"
$stdoutLog = Join-Path $runDirectory "server.stdout.log"
$stderrLog = Join-Path $runDirectory "server.stderr.log"
Copy-Item -LiteralPath $serverBinary -Destination $stagedServer
$newLine = [Environment]::NewLine
[System.IO.File]::WriteAllText(
    (Join-Path $runDirectory "server-config.ini"),
    "port=$Port${newLine}maxplayers=8${newLine}"
)

$processOptions = @{
    FilePath = $stagedServer
    WorkingDirectory = $runDirectory
    RedirectStandardOutput = $stdoutLog
    RedirectStandardError = $stderrLog
    PassThru = $true
    WindowStyle = "Hidden"
}
$serverProcess = Start-Process @processOptions

try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($serverProcess.HasExited) {
            break
        }
        $endpoint = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue |
            Where-Object OwningProcess -eq $serverProcess.Id
        if ($endpoint) {
            $ready = $true
            break
        }
    }
    if (-not $ready) {
        throw "The staged server did not bind UDP port $Port"
    }

    & $soakBinary --host 127.0.0.1 --port $Port --cycles $Cycles --duration-ms $DurationMs
    if ($LASTEXITCODE -ne 0) {
        throw "Two-client soak failed with exit code $LASTEXITCODE; logs: $runDirectory"
    }
    if ($serverProcess.HasExited) {
        throw "Server exited during the soak; logs: $runDirectory"
    }

    Write-Host "Server log: $stdoutLog"
}
finally {
    if (-not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force
    }
}
