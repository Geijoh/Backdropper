# Privacy Policy

Effective date: June 18, 2026

Backdropper is a native Windows utility that customizes image thumbnails locally on your PC.

## Data Collection

Backdropper does not collect, transmit, sell, rent, or share personal data.

The app does not include analytics, telemetry, advertising, tracking pixels, or crash-reporting services.

## Local Files

Backdropper reads image files only when Windows asks it to generate a thumbnail. Thumbnail rendering happens locally on your device.

Image contents are not uploaded to Chris Johnson, GitHub, Microsoft services, or any third-party service by Backdropper.

## Local Settings

Backdropper stores its preferences in the current user's Windows registry under:

```text
HKCU\Software\Backdropper
```

These settings include background mode, colors, checker size, theme preference, and thumbnail cache refresh preference.

## Windows Thumbnail Cache

Windows may store generated thumbnails in the operating system thumbnail cache. If enabled, Backdropper can ask Windows Explorer to restart and can delete local `thumbcache_*.db` files so thumbnails regenerate.

This cache is managed by Windows and stays on your PC.

## Optional Dependencies

Some formats may use optional local renderers installed on your PC, such as Ghostscript for EPS and older PostScript-style AI files. Backdropper does not send files to remote conversion services.

## Network Access

Backdropper does not require network access for thumbnail generation or settings.

The app may open your default browser when you click external links such as GitHub.

If you use the built-in updater, Backdropper contacts GitHub Releases to check the latest available version and download the Windows build ZIP. GitHub may receive standard request information such as your IP address, user agent, and download request metadata under GitHub's own policies.

## Contact

For privacy questions, open an issue at:

https://github.com/Geijoh/Backdropper/issues
