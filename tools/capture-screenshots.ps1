param(
    [string]$ExePath = "$PSScriptRoot\..\build\bin\BackdropperSettings.exe",
    [string]$OutDir = "$PSScriptRoot\..\assets\screenshots"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Build BackdropperSettings.exe first: cmake --build build --config Release"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies "System.Drawing.dll" -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class ScreenshotWin32
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    const int MK_LBUTTON = 0x0001;
    const int SW_RESTORE = 9;
    const int WM_LBUTTONDOWN = 0x0201;
    const int WM_LBUTTONUP = 0x0202;
    const uint RDW_INVALIDATE = 0x0001;
    const uint RDW_UPDATENOW = 0x0100;
    const uint RDW_ALLCHILDREN = 0x0080;
    const uint PW_RENDERFULLCONTENT = 0x00000002;
    static readonly IntPtr DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = new IntPtr(-4);

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr hWnd, IntPtr lprcUpdate, IntPtr hrgnUpdate, uint flags);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr dpiContext);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hWnd);

    public static void SetDpiAware()
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    public static IntPtr FindWindowForPid(int pid)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint windowPid;
            GetWindowThreadProcessId(hWnd, out windowPid);
            if (windowPid == pid && IsWindowVisible(hWnd) && GetWindowTextLength(hWnd) > 0) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static void Activate(IntPtr hWnd)
    {
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
    }

    public static void ClientClick(IntPtr hWnd, int x, int y)
    {
        IntPtr lParam = new IntPtr((y << 16) | (x & 0xffff));
        SendMessage(hWnd, WM_LBUTTONDOWN, new IntPtr(MK_LBUTTON), lParam);
        SendMessage(hWnd, WM_LBUTTONUP, IntPtr.Zero, lParam);
    }

    public static void ParkCursor(IntPtr hWnd)
    {
        RECT rect;
        if (GetWindowRect(hWnd, out rect)) {
            SetCursorPos(rect.Left + 18, rect.Top + 18);
        }
    }

    public static void SaveWindowPng(IntPtr hWnd, string path)
    {
        RedrawWindow(hWnd, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        RECT rect;
        if (!GetWindowRect(hWnd, out rect)) throw new InvalidOperationException("Could not read window bounds.");
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) throw new InvalidOperationException("Window has no size.");

        using (var source = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        using (var sourceGraphics = Graphics.FromImage(source)) {
            IntPtr hdc = sourceGraphics.GetHdc();
            try {
                if (!PrintWindow(hWnd, hdc, PW_RENDERFULLCONTENT)) {
                    throw new InvalidOperationException("PrintWindow failed.");
                }
            } finally {
                sourceGraphics.ReleaseHdc(hdc);
            }

            using (var output = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            using (var outputGraphics = Graphics.FromImage(output))
            using (var brush = new TextureBrush(source, System.Drawing.Drawing2D.WrapMode.Clamp))
            using (var shape = RoundedWindowPath(width, height)) {
                outputGraphics.Clear(Color.Transparent);
                outputGraphics.SmoothingMode = SmoothingMode.AntiAlias;
                outputGraphics.FillPath(brush, shape);

                uint dpi = GetDpiForWindow(hWnd);
                int targetWidth = dpi > 0 ? Math.Max(1, (int)Math.Round(width * 96.0 / dpi)) : width;
                int targetHeight = dpi > 0 ? Math.Max(1, (int)Math.Round(height * 96.0 / dpi)) : height;
                if (targetWidth == width && targetHeight == height) {
                    SaveIfChanged(output, path);
                } else {
                    using (var resized = new Bitmap(targetWidth, targetHeight, PixelFormat.Format32bppArgb))
                    using (var resizedGraphics = Graphics.FromImage(resized)) {
                        resizedGraphics.Clear(Color.Transparent);
                        resizedGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                        resizedGraphics.SmoothingMode = SmoothingMode.AntiAlias;
                        resizedGraphics.DrawImage(output, 0, 0, targetWidth, targetHeight);
                        SaveIfChanged(resized, path);
                    }
                }
            }
        }
    }

    static void SaveIfChanged(Bitmap image, string path)
    {
        if (File.Exists(path)) {
            using (var existing = new Bitmap(path)) {
                if (SamePixels(image, existing)) {
                    return;
                }
            }
        }

        string directory = Path.GetDirectoryName(path);
        if (!String.IsNullOrEmpty(directory)) {
            Directory.CreateDirectory(directory);
        }

        string tempPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp.png";
        image.Save(tempPath, ImageFormat.Png);
        if (File.Exists(path)) {
            File.Delete(path);
        }
        File.Move(tempPath, path);
    }

    static bool SamePixels(Bitmap left, Bitmap right)
    {
        if (left.Width != right.Width || left.Height != right.Height) {
            return false;
        }

        using (var leftArgb = CopyArgb(left))
        using (var rightArgb = CopyArgb(right)) {
            Rectangle rect = new Rectangle(0, 0, left.Width, left.Height);
            BitmapData leftData = leftArgb.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            BitmapData rightData = rightArgb.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                int rowBytes = left.Width * 4;
                byte[] leftRow = new byte[rowBytes];
                byte[] rightRow = new byte[rowBytes];
                for (int y = 0; y < left.Height; ++y) {
                    IntPtr leftPtr = new IntPtr(leftData.Scan0.ToInt64() + y * leftData.Stride);
                    IntPtr rightPtr = new IntPtr(rightData.Scan0.ToInt64() + y * rightData.Stride);
                    Marshal.Copy(leftPtr, leftRow, 0, rowBytes);
                    Marshal.Copy(rightPtr, rightRow, 0, rowBytes);
                    for (int i = 0; i < rowBytes; ++i) {
                        if (leftRow[i] != rightRow[i]) {
                            return false;
                        }
                    }
                }
                return true;
            } finally {
                leftArgb.UnlockBits(leftData);
                rightArgb.UnlockBits(rightData);
            }
        }
    }

    static Bitmap CopyArgb(Bitmap source)
    {
        var copy = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
        using (var graphics = Graphics.FromImage(copy)) {
            graphics.Clear(Color.Transparent);
            graphics.DrawImageUnscaled(source, 0, 0);
        }
        return copy;
    }

    static GraphicsPath RoundedWindowPath(int width, int height)
    {
        // ponytail: over-cut corners and inset edges so captured compositor border pixels stay transparent.
        const float inset = 2f;
        float diameter = Math.Max(1f, (float)Math.Ceiling(width * 32.0 / 1060.0));
        var path = new GraphicsPath();
        path.AddArc(inset, inset, diameter, diameter, 180, 90);
        path.AddArc(width - inset - diameter, inset, diameter, diameter, 270, 90);
        path.AddArc(width - inset - diameter, height - inset - diameter, diameter, diameter, 0, 90);
        path.AddArc(inset, height - inset - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }
}
"@

[ScreenshotWin32]::SetDpiAware()

function Wait-BackdropperWindow($process) {
    for ($i = 0; $i -lt 40; $i++) {
        $hwnd = [ScreenshotWin32]::FindWindowForPid($process.Id)
        if ($hwnd -ne [IntPtr]::Zero) { return $hwnd }
        Start-Sleep -Milliseconds 150
    }
    throw "Backdropper Settings window not found."
}

function Set-DemoSettings {
    $path = "HKCU:\Software\Backdropper"
    $names = @("Mode", "SolidColor", "CheckerColorA", "CheckerColorB", "CheckerSize", "ProtectAppIcons", "DeleteThumbnailDbsOnSave", "CheckUpdatesAutomatically")
    $snapshot = @{
        existed = Test-Path -LiteralPath $path
        values = @{}
    }

    if ($snapshot.existed) {
        foreach ($name in $names) {
            try {
                $snapshot.values[$name] = Get-ItemPropertyValue -LiteralPath $path -Name $name -ErrorAction Stop
            } catch {
                $snapshot.values[$name] = $null
            }
        }
    }

    New-Item -Path $path -Force | Out-Null
    # ponytail: pin demo settings so README screenshots do not depend on local registry state.
    New-ItemProperty -LiteralPath $path -Name Mode -PropertyType String -Value "checker" -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name SolidColor -PropertyType String -Value "#FFFFFF" -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name CheckerColorA -PropertyType String -Value "#FFFFFF" -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name CheckerColorB -PropertyType String -Value "#E5E7EB" -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name CheckerSize -PropertyType DWord -Value 16 -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name ProtectAppIcons -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name DeleteThumbnailDbsOnSave -PropertyType DWord -Value 0 -Force | Out-Null
    New-ItemProperty -LiteralPath $path -Name CheckUpdatesAutomatically -PropertyType DWord -Value 0 -Force | Out-Null

    return $snapshot
}

function Restore-Settings($snapshot) {
    $path = "HKCU:\Software\Backdropper"
    if (-not $snapshot.existed) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
        return
    }

    New-Item -Path $path -Force | Out-Null
    foreach ($name in @("Mode", "SolidColor", "CheckerColorA", "CheckerColorB", "CheckerSize", "ProtectAppIcons", "DeleteThumbnailDbsOnSave", "CheckUpdatesAutomatically")) {
        $value = $snapshot.values[$name]
        if ($null -eq $value) {
            Remove-ItemProperty -LiteralPath $path -Name $name -ErrorAction SilentlyContinue
        } elseif ($value -is [int] -or $value -is [long]) {
            New-ItemProperty -LiteralPath $path -Name $name -PropertyType DWord -Value $value -Force | Out-Null
        } else {
            New-ItemProperty -LiteralPath $path -Name $name -PropertyType String -Value $value -Force | Out-Null
        }
    }
}

$settingsSnapshot = Set-DemoSettings
$app = $null
$previousScreenshotTheme = $env:BACKDROPPER_SCREENSHOT_LIGHT
try {
    $env:BACKDROPPER_SCREENSHOT_LIGHT = "1"
    $app = Start-Process -FilePath $ExePath -PassThru
    $hwnd = Wait-BackdropperWindow $app
    [ScreenshotWin32]::Activate($hwnd)
    Start-Sleep -Milliseconds 900

    [ScreenshotWin32]::ParkCursor($hwnd)
    Start-Sleep -Milliseconds 100
    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-main.png"))

    $rect = [ScreenshotWin32+RECT]::new()
    [ScreenshotWin32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top

    # Open Supported formats from the left pane.
    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 397 / 1060), [int]($height * 406 / 692))
    Start-Sleep -Milliseconds 300

    [ScreenshotWin32]::ParkCursor($hwnd)
    Start-Sleep -Milliseconds 100
    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-supported-formats.png"))

    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 744 / 1060), [int]($height * 586 / 692))
    Start-Sleep -Milliseconds 200

    # Natural layout is 1060x692 DIPs; ratio keeps this DPI-independent.
    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 959 / 1060), [int]($height * 118 / 692))
    Start-Sleep -Milliseconds 300

    [ScreenshotWin32]::ParkCursor($hwnd)
    Start-Sleep -Milliseconds 100
    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-view-menu.png"))

    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 959 / 1060), [int]($height * 118 / 692))
    Start-Sleep -Milliseconds 200

    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 800 / 1060), [int]($height * 20 / 692))
    Start-Sleep -Milliseconds 300

    [ScreenshotWin32]::ParkCursor($hwnd)
    Start-Sleep -Milliseconds 100
    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-about.png"))

    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 617 / 1060), [int]($height * 499 / 692))
    Start-Sleep -Milliseconds 300

    [ScreenshotWin32]::ParkCursor($hwnd)
    Start-Sleep -Milliseconds 100
    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-privacy.png"))
} finally {
    if ($app -and -not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 250
        if (-not $app.HasExited) { $app.Kill() }
    }
    Restore-Settings $settingsSnapshot
    if ($null -eq $previousScreenshotTheme) {
        Remove-Item Env:\BACKDROPPER_SCREENSHOT_LIGHT -ErrorAction SilentlyContinue
    } else {
        $env:BACKDROPPER_SCREENSHOT_LIGHT = $previousScreenshotTheme
    }
}
