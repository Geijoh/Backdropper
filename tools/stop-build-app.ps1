param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath
)

$target = [System.IO.Path]::GetFullPath($ExePath)
$name = [System.IO.Path]::GetFileNameWithoutExtension($target)

Get-Process -Name $name -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ([System.IO.Path]::GetFullPath($_.Path) -ieq $target) } |
    ForEach-Object {
        Stop-Process -Id $_.Id -Force
        Wait-Process -Id $_.Id -Timeout 5 -ErrorAction SilentlyContinue
        Write-Host "Stopped running build app: $target"
    }
