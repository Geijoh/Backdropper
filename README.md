# Backdropper

Native Windows thumbnail utility for transparent images.

Current build: PNG thumbnail handler only. It composites transparent PNG thumbnails over a solid/checker/none background in Explorer.

[Download latest build](https://github.com/Geijoh/Backdropper/releases/latest)

## System Requirements

Runtime:

- Windows 10 or Windows 11 desktop Explorer.
- 64-bit Windows. The thumbnail handler must match Explorer's bitness.
- Per-user registration writes to `HKCU\Software\Classes`; admin rights are not required for the normal dev registration path.
- Settings are stored in `HKCU\Software\Backdropper`.

Build:

- Visual Studio 2022 Build Tools with Desktop development with C++.
- CMake 3.24 or newer.
- x64 build, matching the commands below.

## Screenshots

![Backdropper settings window](assets/screenshots/settings-main.png)

![Backdropper preview view menu](assets/screenshots/settings-view-menu.png)

Refresh screenshots:

```powershell
.\tools\capture-screenshots.ps1
```

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Test

```powershell
.\build\bin\RenderSmoke.exe
```

## Try in Explorer

```powershell
.\tools\register-dev.ps1
```

Open `build\bin\BackdropperSettings.exe` to change background settings or unregister.

## Unregister

```powershell
.\tools\unregister-dev.ps1
```

SVG is intentionally not registered yet. PNG proves the shell-extension path first.
