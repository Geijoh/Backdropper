## Shell

Always prefix shell commands with `rtk`.

## Versioning

Before any commit or push, check `VERSION.md` and bump `Current version` once based on the staged change scope:

- Patch: bug fixes, UI polish, docs, workflow, icon, or internal cleanup.
- Minor: new user-visible capability or supported file type.
- Major: breaking install/registration/settings behavior.

Keep the version in `VERSION.md`; CMake injects it into the app UI.

## Screenshots

Before any commit or push, rebuild Release, run `tools\capture-screenshots.ps1`, and stage changed `assets\screenshots\*.png`.

The screenshot script masks the same rounded window region the app uses so exported PNG corners stay transparent.
