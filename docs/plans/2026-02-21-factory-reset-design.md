# Factory Reset: Remove All Data & Uninstall

**Date**: 2026-02-21
**Status**: Approved

## Problem

After uninstalling and reinstalling Decenza, user data (favourites, settings, presets) persists because:
1. Android Auto Backup silently restores SharedPreferences from Google Drive
2. Public Documents files (`/sdcard/Documents/Decenza/`) survive uninstall
3. Users shouldn't need ADB knowledge to start fresh

## Solution

Add a "Remove All Data & Uninstall" button to Settings > Data tab (left column) with a two-dialog confirmation flow, then wipe all app data and trigger OS uninstall (Android) or quit (other platforms).

## UI Location

Bottom of the left column in `SettingsDataTab.qml`, below the existing "Your Data" section. Red/danger-styled button.

- **Android label**: "Remove All Data & Uninstall"
- **Other platforms**: "Remove All Data & Quit"

## Confirmation Flow

### Dialog 1: "Are you sure?"

- **Title**: "Remove All Data?"
- **Body**: "This will permanently delete ALL your data: settings, favourites, profiles, shot history, themes, and everything else. This cannot be undone."
- **Buttons**: Cancel / Continue (danger styled)

### Dialog 2: "Are you REALLY sure?"

- **Title**: "Are you REALLY sure?"
- **Body**: "This is your last chance. All your espresso data, your carefully dialled-in profiles, your shot history — gone. Poof. Like that time you forgot to put the drip tray back."
- **Buttons**: "I changed my mind" / "Yes, nuke everything" (danger styled)

## What Gets Wiped (in order)

1. **QSettings** (`"DecentEspresso", "DE1Qt"`) — `m_settings.clear()` — favourites, presets, theme, all preferences
2. **Default QSettings** (`"DecentEspresso", "Decenza"`) — `QSettings().clear()` — secondary store (AI, location, profilestorage)
3. **Shot database** — delete `AppDataLocation/shots.db`, `-wal`, `-shm`
4. **Profiles** — delete `AppDataLocation/profiles/` recursively
5. **Widget library** — delete `AppDataLocation/library/` recursively
6. **Skins** — delete `AppDataLocation/skins/` recursively
7. **Translations** — delete `AppDataLocation/translations/` recursively
8. **Screensaver cache** — delete `AppDataLocation/screensaver_videos/` recursively
9. **Debug/crash logs** — delete `*.log` in AppDataLocation
10. **Public Documents** — delete `Documents/Decenza/`, `Documents/Decenza Backups/`, `Documents/ai_logs/`
11. **Cache** — delete everything in CacheLocation

## Post-Wipe Behavior

- **Android**: Launch `ACTION_DELETE` intent (`package:io.github.kulitorum.decenza_de1`) to open system uninstall dialog, then `QCoreApplication::quit()`
- **All other platforms**: `QCoreApplication::quit()`

## Implementation Touch Points

| File | Change |
|------|--------|
| `src/core/settings.h/.cpp` | Add `Q_INVOKABLE void factoryReset()` — steps 1-11 |
| `src/controllers/maincontroller.h/.cpp` | Add `Q_INVOKABLE void factoryResetAndQuit()` — calls factoryReset(), triggers quit/uninstall |
| `android/.../StorageHelper.java` | Add `requestUninstall()` using `ACTION_DELETE` intent |
| `qml/pages/settings/SettingsDataTab.qml` | Button + two Dialog components |

## Android Uninstall Intent

```java
Intent intent = new Intent(Intent.ACTION_DELETE);
intent.setData(Uri.parse("package:" + context.getPackageName()));
startActivity(intent);
```

This shows the native system uninstall confirmation. The OS handles cleanup of internal app data after uninstall.
