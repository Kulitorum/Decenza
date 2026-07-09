## Data Migration (Device-to-Device Transfer)

Transfer profiles, shots, settings, media, and AI conversations between devices over WiFi.

### Architecture
- **Server**: `ShotServer` (same as Remote Access) exposes `/api/backup/*` REST endpoints
- **Client**: `DataMigrationClient` discovers servers and imports data
- **Discovery**: UDP broadcast on port 8889 for automatic device finding, with manual IP:port fallback
- **Authentication**: TOTP-based authentication when server requires it (QR code + manual code entry, session token caching)
- **Default port**: 8888 (configurable in Settings → Shot History → Port)
- **UI**: Settings → Data tab

### Key Files
- `src/core/datamigrationclient.cpp/.h` - Client-side discovery, authentication, and import
- `src/network/shotserver_backup.cpp` - Server-side backup endpoint handlers
- `src/network/shotserver.cpp` - Route dispatch (routes `/api/backup/*` to handlers)
- `qml/pages/settings/SettingsDataTab.qml` - UI

### How It Works
1. **Source device**: Enable "Remote Access" in Settings → Shot History tab
2. **Target device**: Go to Settings → Data tab, tap "Search"
3. Client broadcasts UDP discovery packet (`DECENZA_DISCOVER`) to port 8889
4. Servers respond with device info (name, URL, platform)
5. Client filters out own device (compares sender IP against local network interfaces), shows discovered servers
6. If discovery fails, user can enter IP:port manually (e.g., `192.168.1.100:8888`)
7. If the server requires authentication, user is prompted for a TOTP code
8. User selects device and imports desired data types

### REST Endpoints (on ShotServer)
- `GET /api/backup/manifest` - List available data (counts, sizes; incl. `recipeCount`/`bagCount`/`equipmentCount`)
- `GET /api/backup/settings` - Export settings JSON (includes SAW learning; excludes machine flow calibration + mqttPassword on import)
- `GET /api/backup/extra-settings` - Export extra QSettings (shot-map location, accessibility, language) — LAN parity with the full archive's `extra_settings.json`
- `GET /api/backup/profiles` - List profile filenames
- `GET /api/backup/profile/{category}/{filename}` - Download single profile
- `GET /api/backup/shots` - List shot IDs
- `GET /api/backup/media` - List media files
- `GET /api/backup/media/{filename}` - Download media file
- `GET /api/backup/ai-conversations` - Export AI conversations
- `GET /api/backup/full` - Download full backup archive
- `POST /api/backup/restore` - Restore from backup archive

### Import Options
- **Import All**: Settings, profiles, shots, media, AI conversations
- **Individual**: Import only specific data types (Settings, Profiles, Shots, Media, AI Conversations buttons)

### Coverage parity (finish-recipes-first-class)
Every durable store is now both exported and imported on **both** paths (LAN device-to-device and `.dcbackup` full archive):
- **SAW learning** (`saw/*` keys) rides `SettingsSerializer` (`sawLearningExport`/`sawLearningImport`), so it lands in the LAN settings **and** the archive `settings.json`. Scale+profile specific and portable — deliberately NOT excluded like the machine-specific flow calibration.
- **Extra settings** (shot-map location, accessibility, language): the LAN client now fetches `/api/backup/extra-settings` (chained after the settings import), matching what the archive already bundled.
- **Recipes** are import-side only — they already exported inside `shots.db`; the fix was the missing merge (`importRecipesStatic`).
- **Flow calibration** and **mqttPassword** remain intentionally excluded on import (machine/broker specific).

### What rides inside "Shots"
The Shots import ships the whole `shots.db` file, so several tables travel and are row-remapped during the merge (`ShotHistoryStorage::importDatabaseStatic`), in order: **equipment** (`importEquipmentStatic`) → **coffee bags** (`importBagsStatic`, remaps `equipment_id`) → **recipes** (`importRecipesStatic`, remaps `equipment_id`; finish-recipes-first-class) → **shots** (remaps `equipment_id`, `bag_id`, `recipe_id`, and carries `steam_json`/`hot_water_json`). These are not separate UI buttons — they fold into "Shots". A backup with **zero shots** skips the whole merge (early-return), so bags/equipment/recipes only transfer alongside shots.
