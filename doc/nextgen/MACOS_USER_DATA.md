# macOS user data

The signed `RoR.app` bundle is immutable application code and resources. The
runtime must never create configuration, cache, log, mod, or generated shader
files below `RoR.app/Contents`.

For a new packaged installation, writable data uses the standard per-user
macOS Library directories:

| Data | Directory |
| --- | --- |
| Configuration, mods, projects, saves, and screenshots | `~/Library/Application Support/Rigs of Rods` |
| Generated cache and repository thumbnails | `~/Library/Caches/Rigs of Rods` |
| Runtime logs | `~/Library/Logs/Rigs of Rods` |

Path selection happens before logging and before `RoR.cfg` is loaded. A
`config` directory accidentally present beside an app-bundle executable is
ignored, so it cannot redirect writes into the signed bundle.

Existing macOS installations are migration-safe: when the Application Support
directory does not exist but `~/RigsOfRods` contains configuration, mods,
projects, saves, or another user-content directory, packaged builds continue
to use `~/RigsOfRods` for configuration and user content. A stale legacy
directory containing only logs does not redirect a new installation. Caches
and logs move to their standard Library locations in either case. If both
user-data directories exist, Application Support wins deterministically.

Non-bundle development builds preserve the historical behavior:

- `<executable directory>/config` selects portable mode when that directory
  already exists.
- Otherwise the development user directory remains `~/RigsOfRods`.

This preserves local developer workflows without allowing a packaged build to
mutate or invalidate its own code signature.
