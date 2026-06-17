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
using System.Runtime.InteropServices;

public static class ScreenshotWin32
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    const int MK_LBUTTON = 0x0001;
    const int SW_RESTORE = 9;
    const int WM_LBUTTONDOWN = 0x0201;
    const int WM_LBUTTONUP = 0x0202;
    const uint PW_RENDERFULLCONTENT = 0x00000002;
    static readonly IntPtr DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = new IntPtr(-4);

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr dpiContext);

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

    public static void SaveWindowPng(IntPtr hWnd, string path)
    {
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
                output.Save(path, ImageFormat.Png);
            }
        }
    }

    static GraphicsPath RoundedWindowPath(int width, int height)
    {
        // Matches CreateRoundRectRgn(..., Px(18), Px(18)) in the app at any DPI.
        float diameter = Math.Max(1f, (float)Math.Round(width * 18.0 / 1060.0));
        var path = new GraphicsPath();
        path.AddArc(0, 0, diameter, diameter, 180, 90);
        path.AddArc(width - diameter, 0, diameter, diameter, 270, 90);
        path.AddArc(width - diameter, height - diameter, diameter, diameter, 0, 90);
        path.AddArc(0, height - diameter, diameter, diameter, 90, 90);
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

$app = Start-Process -FilePath $ExePath -PassThru
try {
    $hwnd = Wait-BackdropperWindow $app
    [ScreenshotWin32]::Activate($hwnd)
    Start-Sleep -Milliseconds 500

    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-main.png"))

    $rect = [ScreenshotWin32+RECT]::new()
    [ScreenshotWin32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    # Natural layout is 1060x692 DIPs; ratio keeps this DPI-independent.
    [ScreenshotWin32]::ClientClick($hwnd, [int]($width * 959 / 1060), [int]($height * 118 / 692))
    Start-Sleep -Milliseconds 300

    [ScreenshotWin32]::SaveWindowPng($hwnd, (Join-Path $OutDir "settings-view-menu.png"))
} finally {
    if ($app -and -not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 250
        if (-not $app.HasExited) { $app.Kill() }
    }
}
