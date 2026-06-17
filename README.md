# Backdropper

Native Windows thumbnail utility for transparent images.

Current build: PNG thumbnail handler only. It composites transparent PNG thumbnails over a solid/checker/none background in Explorer.

## Screenshots

![Backdropper settings window](assets/screenshots/settings-main.png)

![Backdropper preview view menu](assets/screenshots/settings-view-menu.png)

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
