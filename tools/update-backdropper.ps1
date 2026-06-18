param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir,

    [int]$CurrentPid = 0,

    [string]$CurrentVersion = '0.0.0',

    [string]$Repo = 'Geijoh/Backdropper'
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host "[Backdropper] $Message"
}

function Start-Backdropper {
    $exe = Join-Path $InstallDir 'BackdropperSettings.exe'
    if (Test-Path -LiteralPath $exe) {
        Start-Process -FilePath $exe -WorkingDirectory $InstallDir
    }
}

function Copy-Payload {
    param([string]$PayloadDir)
    Copy-Item -Path (Join-Path $PayloadDir '*') -Destination $InstallDir -Recurse -Force
}

function Ensure-Explorer {
    if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue)) {
        Start-Process explorer.exe
    }
}

$tempRoot = Join-Path $env:TEMP ("BackdropperUpdate-" + [guid]::NewGuid().ToString('N'))
$stoppedExplorer = $false

try {
    if (-not (Test-Path -LiteralPath $InstallDir)) {
        throw "Install directory does not exist: $InstallDir"
    }
    $writeProbe = Join-Path $InstallDir '.backdropper-update-write-test'
    try {
        Set-Content -LiteralPath $writeProbe -Value 'ok' -NoNewline
        Remove-Item -LiteralPath $writeProbe -Force
    } catch {
        throw "Install directory is not writable by the current user: $InstallDir"
    }

    Write-Step "Checking latest GitHub release..."
    $headers = @{ 'User-Agent' = 'Backdropper-Updater' }
    $releaseBaseUrl = "https://github.com/$Repo/releases/latest/download"
    $versionUrl = "$releaseBaseUrl/backdropper-version.txt"
    $assetName = 'Backdropper-latest-windows-x64.zip'
    $assetUrl = "$releaseBaseUrl/$assetName"
    $latestVersion = ((Invoke-RestMethod -Uri $versionUrl -Headers $headers) -replace '^v', '').Trim()

    try {
        if ([version]$latestVersion -le [version]$CurrentVersion) {
            Write-Step "Already up to date. Current version: $CurrentVersion."
            Start-Sleep -Seconds 2
            Start-Backdropper
            exit 0
        }
    } catch {
        Write-Step "Could not compare versions; continuing with the latest release asset."
    }

    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    $zipPath = Join-Path $tempRoot $assetName
    $extractDir = Join-Path $tempRoot 'extract'

    Write-Step "Downloading $assetName..."
    Invoke-WebRequest -Uri $assetUrl -Headers $headers -OutFile $zipPath

    Write-Step "Extracting update..."
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    $settingsExe = Get-ChildItem -Path $extractDir -Recurse -Filter BackdropperSettings.exe | Select-Object -First 1
    if (-not $settingsExe) {
        throw "The downloaded ZIP does not contain BackdropperSettings.exe."
    }
    $payloadDir = Split-Path -Parent $settingsExe.FullName

    if ($CurrentPid -gt 0) {
        Write-Step "Waiting for Backdropper to close..."
        Wait-Process -Id $CurrentPid -Timeout 30 -ErrorAction SilentlyContinue
    }

    Write-Step "Replacing Backdropper files..."
    try {
        Copy-Payload -PayloadDir $payloadDir
    } catch {
        Write-Step "Files are still in use. Restarting shell thumbnail hosts and retrying..."
        Stop-Process -Name prevhost,dllhost -Force -ErrorAction SilentlyContinue
        Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
        $stoppedExplorer = $true
        Start-Sleep -Seconds 2
        Copy-Payload -PayloadDir $payloadDir
    }

    if ($stoppedExplorer) {
        Ensure-Explorer
    }

    Write-Step "Update complete. Starting Backdropper..."
    Start-Backdropper
} catch {
    Write-Host "[Backdropper] Update failed: $($_.Exception.Message)"
    if ($stoppedExplorer) {
        Ensure-Explorer
    }
    Start-Backdropper
    Read-Host 'Press Enter to close'
    exit 1
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
