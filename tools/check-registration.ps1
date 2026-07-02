$ErrorActionPreference = 'Stop'

$clsid = '{7F08B58C-8D1C-44D3-9A73-AB554FF53B1D}'
$thumbHandler = '{E357FCCD-A995-4576-B01F-234630154E96}'
$extensions = '.png', '.webp', '.gif', '.ico', '.svg', '.psd', '.ai', '.eps', '.pdf', '.avif', '.tga', '.dds'
$expectedDll = (Resolve-Path (Join-Path $PSScriptRoot '..\build\bin\BackdropperThumb.dll') -ErrorAction SilentlyContinue).Path

function Get-DefaultValue($root, $path) {
    $key = $root.OpenSubKey($path)
    if (-not $key) { return $null }
    try { return $key.GetValue('') } finally { $key.Close() }
}

$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$hkcr = [Microsoft.Win32.Registry]::ClassesRoot
$registeredDll = Get-DefaultValue $hkcu "Software\Classes\CLSID\$clsid\InprocServer32"
$settingsKey = $hkcu.OpenSubKey('Software\Backdropper')

Write-Host "Expected DLL:   $expectedDll"
Write-Host "Registered DLL: $registeredDll"
if ($expectedDll -and $registeredDll -and $expectedDll -ine $registeredDll) {
    Write-Warning "Backdropper CLSID points at a different DLL. Re-register this build."
}
Write-Host ''

$rows = $extensions | ForEach-Object {
    $ext = $_
    $enabledName = "Format$($ext.Replace('.', '_').ToUpperInvariant())"
    $enabled = $true
    if ($settingsKey) {
        $enabledValue = $settingsKey.GetValue($enabledName)
        if ($null -ne $enabledValue) {
            $enabled = [int]$enabledValue -ne 0
        }
    }
    $progId = Get-DefaultValue $hkcr $ext
    $extensionHandler = Get-DefaultValue $hkcr "$ext\shellex\$thumbHandler"
    $progIdHandler = if ($progId) { Get-DefaultValue $hkcr "$progId\shellex\$thumbHandler" } else { $null }
    $active = ($extensionHandler -ieq $clsid) -or ($progIdHandler -ieq $clsid)

    [pscustomobject]@{
        Extension = $ext
        Enabled = $enabled
        ProgID = $progId
        ExtensionHandler = $extensionHandler
        ProgIDHandler = $progIdHandler
        Active = $active
        Unexpected = -not $enabled -and $active
    }
}

if ($settingsKey) {
    $settingsKey.Close()
}

$rows | Select-Object Extension, Enabled, Active, ProgID, ExtensionHandler, ProgIDHandler | Format-Table -AutoSize

$unexpected = @($rows | Where-Object { $_.Unexpected })
if ($unexpected.Count -gt 0) {
    Write-Error "Backdropper is still registered for disabled format(s): $($unexpected.Extension -join ', ')"
}
