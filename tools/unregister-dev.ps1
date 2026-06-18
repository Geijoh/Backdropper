$ErrorActionPreference = 'Stop'
$dll = Join-Path $PSScriptRoot '..\build\bin\BackdropperThumb.dll'
if (!(Test-Path $dll)) {
    throw "DLL not found: $dll"
}
$resolved = (Resolve-Path $dll).Path
$process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/u /s `"$resolved`"" -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "regsvr32 /u failed with exit code $($process.ExitCode)"
}
Write-Host "Backdropper thumbnail handlers unregistered for current user."
