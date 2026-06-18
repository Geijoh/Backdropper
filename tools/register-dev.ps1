$ErrorActionPreference = 'Stop'
$dll = Join-Path $PSScriptRoot '..\build\bin\BackdropperThumb.dll'
if (!(Test-Path $dll)) {
    throw "Build first: cmake --build build --config Release"
}
$resolved = (Resolve-Path $dll).Path
$process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/s `"$resolved`"" -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($process.ExitCode)"
}
Write-Host "Backdropper thumbnail handlers registered for current user."
