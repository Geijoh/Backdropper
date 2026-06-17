# Backdropper Windows Thumbnail Background Plan

Date: 2026-06-17
Status: draft

## Goal

Make transparent raster and vector images easier to recognize in Windows Explorer by rendering thumbnails over a user-selected background.

Supported first:
- PNG with alpha
- SVG

Supported later:
- GIF/TIFF/BMP through WIC if useful
- WebP/AVIF only when system codecs are present and testing proves value

Skipped by default:
- JPEG. No transparency problem.
- A service/tray app. Explorer calls thumbnail handlers on demand.
- Custom thumbnail cache. Windows already has one.

## Platform Decision

Use a Windows Shell thumbnail provider.

Explorer thumbnails are not a normal app surface. To change them, register a COM thumbnail handler for specific file extensions. The handler implements:
- `IThumbnailProvider`
- `IInitializeWithStream`

Keep stream initialization. Microsoft calls it the safer/reliable path for isolated thumbnail providers.

## Recommended Architecture

```text
Explorer
  -> Backdropper.ThumbProvider.dll
      -> reads HKCU\Software\Backdropper settings
      -> decodes image stream
      -> composites background
      -> returns HBITMAP

Backdropper.Settings.exe
  -> native Windows settings UI
  -> writes registry settings
  -> registers/unregisters per-user shell extension
```

Projects:
- `src/ThumbProvider`: C++17 Win32 COM DLL.
- `src/Settings`: WinUI 3 settings app, preferably C# for speed unless we want all-C++.
- `src/RenderSmoke`: tiny CLI smoke test that renders a file to a bitmap and checks pixels.

Ponytail choice: registry settings, not JSON. The handler and settings UI both get the Windows Registry for free.

## Registry Shape

Settings:

```text
HKCU\Software\Backdropper
  Mode = "checker" | "solid" | "none"
  SolidColor = "#FFFFFFFF"
  CheckerColorA = "#FFFFFFFF"
  CheckerColorB = "#FFE5E7EB"
  CheckerSize = 8
```

Shell registration:

```text
HKCU\Software\Classes\.png\shellex\{E357FCCD-A995-4576-B01F-234630154E96} = {Backdropper CLSID}
HKCU\Software\Classes\.svg\shellex\{E357FCCD-A995-4576-B01F-234630154E96} = {Backdropper CLSID}
HKCU\Software\Classes\CLSID\{Backdropper CLSID}\InprocServer32 = path\Backdropper.ThumbProvider.dll
```

Installer must save previous handler values and restore them on uninstall.

## Rendering Pipeline

1. Receive `IStream` and requested max size `cx`.
2. Detect type from stream bytes:
   - PNG signature for PNG.
   - XML/SVG sniff for SVG.
3. Decode:
   - PNG/raster: WIC.
   - SVG: Direct2D SVG document APIs.
4. Compute output dimensions:
   - longest side <= `cx`.
   - preserve aspect ratio.
   - do not pad to square. Windows owns outer padding/adornment.
5. Draw background:
   - solid fill, checker fill, or no fill.
6. Draw image over background.
7. Return `HBITMAP`.
   - opaque background: `WTSAT_RGB`.
   - mode `none`: `WTSAT_ARGB`.

## Native UI Scope

First useful settings screen:
- Enable Backdropper for PNG
- Enable Backdropper for SVG
- Background mode segmented control: none, solid, checker
- Color picker for solid color
- Two color pickers and size field for checkerboard
- Apply button
- Restore Windows defaults button

No accounts, no profiles, no theme editor.

## Implementation Phases

### Phase 1: Shell Spike

Create the COM thumbnail provider with hardcoded checkerboard PNG rendering.

Files:
- Create `D:\Vibe Projects\Backdropper\src\ThumbProvider\...`
- Create `D:\Vibe Projects\Backdropper\tools\register-dev.ps1`
- Create `D:\Vibe Projects\Backdropper\tools\unregister-dev.ps1`

Success:
- Register for `.png` under HKCU.
- Explorer shows transparent PNG over checkerboard.
- Unregister restores previous handler.

### Phase 2: Renderer

Add the real renderer.

Files:
- Create `D:\Vibe Projects\Backdropper\src\ThumbProvider\render_png.cpp`
- Create `D:\Vibe Projects\Backdropper\src\ThumbProvider\render_svg.cpp`
- Create `D:\Vibe Projects\Backdropper\testdata\transparent.png`
- Create `D:\Vibe Projects\Backdropper\testdata\transparent.svg`
- Create `D:\Vibe Projects\Backdropper\src\RenderSmoke\...`

Success:
- PNG transparent pixels show chosen background.
- SVG transparent areas show chosen background.
- Bad files fail cleanly without crashing Explorer host.

### Phase 3: Settings UI

Create the small native settings app.

Files:
- Create `D:\Vibe Projects\Backdropper\src\Settings\...`

Success:
- UI writes registry values.
- Toggle registers/unregisters `.png` and `.svg`.
- Changes affect newly generated thumbnails.

### Phase 4: Packaging

Start with dev scripts. Add installer only when the utility is worth distributing.

Options:
- Dev: PowerShell scripts. Fastest.
- Release: MSI or MSIX with external location if shell registration works cleanly.

Success:
- Per-user install works without admin.
- Uninstall restores previous handlers.

## Tests

Small checks only:
- `RenderSmoke transparent.png checker` asserts a known transparent pixel becomes checker color.
- `RenderSmoke transparent.svg solid` asserts transparent pixel becomes solid color.
- `RenderSmoke corrupt.png` returns failure, no crash.
- Manual Explorer smoke test in a folder with PNG and SVG fixtures.

## Risks

- Explorer thumbnail cache can keep old images after settings change.
  - Start with `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)`.
  - Add cache-clearing UI only if users hit stale thumbnails often.
- Registering for `.png` replaces the built-in thumbnail handler.
  - We must render PNG ourselves and restore the previous value on uninstall.
- SVG support varies by renderer.
  - Use Direct2D first. Document unsupported SVG features instead of bundling a browser engine.
- Thumbnail handlers process untrusted files.
  - No network fetches.
  - No external tools.
  - Keep process isolation.
  - Cap output size to requested `cx`.

## Open Questions

1. App UI stack: WinUI 3 C# for speed, or all-C++ for one runtime?
2. Default background: checkerboard or light solid?
3. Should the MVP register `.svg` immediately, or ship PNG first and add SVG after the renderer spike?

## Sources Checked

- Microsoft: [Building Thumbnail Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers)
- Microsoft: [Thumbnail Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/thumbnail-providers)
- Microsoft: [IThumbnailProvider](https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider)
- Microsoft: [Windows Imaging Component Overview](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec)
- Microsoft: [WinUI 3](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/)
- Microsoft: [Direct2D SVG support](https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support)
