# Backdropper

![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/Geijoh/Backdropper/total)

Native Windows thumbnail utility for transparent images.

Current build: WIC-backed thumbnail handler with native SVG/PDF rendering plus built-in PSD and TGA fallback decoding. It composites transparent thumbnails over a solid/checker/none background in Explorer.

Supported/targeted extensions: `.png`, `.webp`, `.gif`, `.ico`, `.svg`, `.psd`, `.ai`, `.eps`, `.pdf`, `.avif`, `.tga`, `.dds`.

Backdropper registers SVG, PDF, PDF-compatible AI, PSD, and TGA through built-in fallback renderers. EPS/PostScript AI register when Ghostscript is installed. Other formats are registered only when Windows has an installed Windows Imaging Component decoder, so unsupported formats keep their existing Explorer behavior.

[Download latest build](https://github.com/Geijoh/Backdropper/releases/latest) | [Privacy policy](PRIVACY.md)

Release builds are code-signed with Azure Artifact Signing.

Use **Supported formats** in the app to choose which extensions Backdropper registers. EPS and legacy PostScript-style AI require [Ghostscript](https://ghostscript.com/releases/gsdnld.html); EPS is disabled automatically when Ghostscript is not installed.

Windows can ask thumbnail handlers for taskbar and Start app icons. Backdropper always skips backgrounds for those app-icon requests.

Use **Check for updates** in the app to check GitHub Releases. When a newer build is available, Backdropper can download the latest Windows x64 build, verify the signed binaries, replace the current files, and relaunch. The install folder must be writable by the current user.

## System Requirements

Runtime:

- Windows 10 or Windows 11 desktop Explorer.
- 64-bit Windows. The thumbnail handler must match Explorer's bitness.
- Per-user registration writes to `HKCU\Software\Classes`; admin rights are not required for the normal dev registration path.
- Settings are stored in `HKCU\Software\Backdropper`.
- Updating from the app requires network access to GitHub Releases and write access to the Backdropper install folder.
- Optional: Ghostscript enables EPS and older PostScript-style AI thumbnails.

Build:

- Visual Studio 2022 Build Tools with Desktop development with C++.
- CMake 3.24 or newer.
- x64 build, matching the commands below.

## Screenshots

![Backdropper settings window](assets/screenshots/settings-main.png)

![Backdropper supported formats dialog](assets/screenshots/settings-supported-formats.png)

![Backdropper preview view menu](assets/screenshots/settings-view-menu.png)

![Backdropper about dialog](assets/screenshots/settings-about.png)

![Backdropper privacy policy dialog](assets/screenshots/settings-privacy.png)

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

## Release Signing

GitHub Actions signs release binaries with Azure Artifact Signing on pushes to `main`.

Repository variables:

- `AZURE_CLIENT_ID`
- `AZURE_TENANT_ID`
- `AZURE_SUBSCRIPTION_ID`
- `AZURE_ARTIFACT_SIGNING_ENDPOINT`
- `AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME`
- `AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE_NAME`

No committed `metadata.json` is required.

## Try in Explorer

```powershell
.\tools\register-dev.ps1
```

Open `build\bin\BackdropperSettings.exe` to change background settings or unregister.

Check the active thumbnail registration:

```powershell
.\tools\check-registration.ps1
```

## Unregister

```powershell
.\tools\unregister-dev.ps1
```

SVG and PDF render through native Windows APIs. PDF-compatible AI uses the same PDF renderer. PSD and TGA have built-in flattened-thumbnail fallback support. EPS and older PostScript-style AI use Ghostscript when installed. AVIF, DDS, and WebP support depends on installed WIC codecs.

## Update Script

Release ZIPs include `BackdropperUpdater.exe`. The app uses this helper for update actions. It reads the latest release version, downloads the matching version-numbered Windows x64 ZIP, verifies that the shipping binaries are signed by the expected publisher, waits for the settings app to close, replaces files in-place, and starts Backdropper again. If Explorer has the handler DLL locked, the updater restarts Explorer and retries the replacement.

## Development Note

Backdropper was designed and built with AI-assisted development tools, including Claude Code, Codex, and Claude Design, under maintainer review and direction. The shipped code, packaging, and releases are maintained by Chris Johnson.
