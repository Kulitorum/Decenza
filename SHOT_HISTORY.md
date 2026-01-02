# Shot History & Comparison Feature

Local storage and comparison system for DE1 espresso shots.

## Overview

The shot history feature stores every espresso shot locally on the device, enabling users to:
- Browse and filter past shots
- Compare up to 3 shots side-by-side
- Search notes and filter by any metadata field
- Export debug logs for troubleshooting

All shots are saved automatically when extraction completes. No cloud connection required.

## Architecture

### Storage Backend

**SQLite** was chosen over JSON files for:
- Fast indexed queries on old tablets
- Efficient filtering without loading all data
- Full-text search capability
- Single file backup

Database location:
- **Android**: `/data/data/io.github.kulitorum.decenza_de1/files/shots.db`
- **Windows**: `%APPDATA%/DecentEspresso/DE1Qt/shots.db`
- **macOS**: `~/Library/Application Support/DecentEspresso/DE1Qt/shots.db`
- **Linux**: `~/.local/share/DecentEspresso/DE1Qt/shots.db`

### Database Schema

```sql
-- Main shots table (all filterable fields indexed)
CREATE TABLE shots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT UNIQUE NOT NULL,
    timestamp INTEGER NOT NULL,              -- Unix timestamp

    -- Profile snapshot
    profile_name TEXT NOT NULL,
    profile_json TEXT,                       -- Full profile for reference

    -- Shot metrics
    duration_seconds REAL NOT NULL,
    final_weight REAL,
    dose_weight REAL,

    -- DYE metadata
    bean_brand TEXT,
    bean_type TEXT,
    roast_date TEXT,                         -- YYYY-MM-DD
    roast_level TEXT,
    grinder_model TEXT,
    grinder_setting TEXT,
    drink_tds REAL,
    drink_ey REAL,
    enjoyment INTEGER,                       -- 0-100
    espresso_notes TEXT,
    barista TEXT,

    -- Visualizer integration
    visualizer_id TEXT,
    visualizer_url TEXT,

    -- Debug log (captured during shot)
    debug_log TEXT
);

-- Time-series data as compressed BLOB (~5-10KB per shot)
CREATE TABLE shot_samples (
    shot_id INTEGER PRIMARY KEY REFERENCES shots(id) ON DELETE CASCADE,
    sample_count INTEGER NOT NULL,
    data_blob BLOB NOT NULL                  -- zlib-compressed JSON
);

-- Phase markers
CREATE TABLE shot_phases (
    shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
    time_offset REAL NOT NULL,
    label TEXT NOT NULL,
    frame_number INTEGER,
    is_flow_mode INTEGER DEFAULT 0
);

-- Full-text search on notes and bean info
CREATE VIRTUAL TABLE shots_fts USING fts5(
    espresso_notes, bean_brand, bean_type,
    content='shots', content_rowid='id'
);
```

### Data Compression

Time-series data (~600 samples at 5Hz) is stored as a zlib-compressed JSON blob:

```json
{
  "pressure": {"t": [0, 0.2, ...], "v": [0, 1.2, ...]},
  "flow": {"t": [...], "v": [...]},
  "temperature": {"t": [...], "v": [...]},
  "weight": {"t": [...], "v": [...]},
  "pressureGoal": {"t": [...], "v": [...]},
  "flowGoal": {"t": [...], "v": [...]},
  "temperatureGoal": {"t": [...], "v": [...]}
}
```

Compression ratio: ~30KB raw → ~5-10KB compressed

## C++ Classes

### ShotHistoryStorage

**Location**: `src/history/shothistorystorage.h/.cpp`

Main database interface exposed to QML.

```cpp
class ShotHistoryStorage : public QObject {
    Q_PROPERTY(int totalShots READ totalShots NOTIFY totalShotsChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)

    // Save shot (called automatically from MainController)
    qint64 saveShot(ShotDataModel* data, const Profile* profile,
                    double duration, double finalWeight, double doseWeight,
                    const ShotMetadata& metadata, const QString& debugLog);

    // Query methods (paginated)
    Q_INVOKABLE QVariantList getShots(int offset = 0, int limit = 50);
    Q_INVOKABLE QVariantList getShotsFiltered(const QVariantMap& filter,
                                               int offset = 0, int limit = 50);
    Q_INVOKABLE QVariantMap getShot(qint64 shotId);

    // Filter dropdown options
    Q_INVOKABLE QStringList getDistinctProfiles();
    Q_INVOKABLE QStringList getDistinctBeanBrands();
    Q_INVOKABLE QStringList getDistinctBeanTypes();
    Q_INVOKABLE QStringList getDistinctGrinders();

    // Visualizer sync
    Q_INVOKABLE bool updateVisualizerInfo(qint64 shotId,
                                           const QString& visualizerId,
                                           const QString& visualizerUrl);

    // Delete
    Q_INVOKABLE bool deleteShot(qint64 shotId);

    // Export for bug reports
    Q_INVOKABLE QString exportShotData(qint64 shotId);
};
```

#### Filter Options

Pass a QVariantMap to `getShotsFiltered()`:

```javascript
var filter = {
    profileName: "Adaptive v2",      // Exact match
    beanBrand: "Counter Culture",    // Exact match
    beanType: "Ethiopian",           // Exact match
    grinderModel: "Niche Zero",      // Exact match
    roastLevel: "Light",             // Exact match
    minEnjoyment: 70,                // Rating >= 70
    maxEnjoyment: 100,               // Rating <= 100
    dateFrom: 1704067200,            // Unix timestamp
    dateTo: 1704153600,              // Unix timestamp
    searchText: "fruity bright",     // Full-text search in notes
    onlyWithVisualizer: false        // Only uploaded shots
}
```

### ShotDebugLogger

**Location**: `src/history/shotdebuglogger.h/.cpp`

Captures diagnostic information during shot extraction.

```cpp
class ShotDebugLogger : public QObject {
    void startCapture();   // Called on espressoCycleStarted
    void stopCapture();    // Called on shotEnded
    QString getCapturedLog() const;

    // Log methods (thread-safe)
    void logBleEvent(const QString& event);
    void logStateTransition(const QString& from, const QString& to);
    void logSampleAnomaly(const QString& desc, double value);
    void logScaleReading(double weight, double flowRate);
    void logFrameChange(int from, int to, const QString& name);
    void logWarning(const QString& msg);
    void logError(const QString& msg);
};
```

#### Log Format

```
[0.000] START Shot capture started - 2026-01-02T14:35:22
[0.000] INFO Profile: Adaptive v2
[0.000] PHASE EspressoPreheating (5)
[2.150] DE1 state=4 subState=1
[5.230] PHASE Preinfusion (6)
[5.230] FRAME 0 -> 1 "Preinfusion"
[5.500] WEIGHT 0.5g
[8.450] ANOMALY Pressure spike: 2.5 -> 5.1 bar (delta=2.6): 2.60
[10.000] SAMPLE t=10.0s P=6.2bar F=2.1mL/s T=92.3C
[12.100] PHASE Pouring (7)
[12.100] FRAME 1 -> 2 "Extraction"
[15.000] SAMPLE t=15.0s P=9.0bar F=2.3mL/s T=93.1C
...
[35.200] PHASE Ending (8)
[38.500] STOP Shot capture stopped - 156 samples recorded
```

### ShotComparisonModel

**Location**: `src/models/shotcomparisonmodel.h/.cpp`

Manages shot selection and data for comparison view.

```cpp
class ShotComparisonModel : public QObject {
    Q_PROPERTY(int shotCount READ shotCount NOTIFY shotsChanged)
    Q_PROPERTY(QVariantList shots READ shotsVariant NOTIFY shotsChanged)
    Q_PROPERTY(double maxTime READ maxTime NOTIFY shotsChanged)

    // Add/remove shots (max 3)
    Q_INVOKABLE bool addShot(qint64 shotId);
    Q_INVOKABLE void removeShot(qint64 shotId);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE bool hasShotId(qint64 shotId) const;

    // Get data for specific shot index (0, 1, or 2)
    Q_INVOKABLE QVariantList getPressureData(int index) const;
    Q_INVOKABLE QVariantList getFlowData(int index) const;
    Q_INVOKABLE QVariantList getTemperatureData(int index) const;
    Q_INVOKABLE QVariantList getWeightData(int index) const;
    Q_INVOKABLE QVariantMap getShotInfo(int index) const;

    // Colors (green, blue, orange)
    Q_INVOKABLE QColor getShotColor(int index) const;
};
```

## QML Pages

### ShotHistoryPage

**Location**: `qml/pages/ShotHistoryPage.qml`

Main list view for browsing shots.

**Features**:
- Filter dropdowns (Profile, Bean Brand)
- Text search in notes
- Checkbox selection (max 3 shots)
- Compare button (enabled when 2+ selected)
- Tap to select, long-press for details
- Shot cards show: date, profile, bean, rating, dose→output, duration

**Navigation**: IdlePage → History button → ShotHistoryPage

### ShotDetailPage

**Location**: `qml/pages/ShotDetailPage.qml`

Full view of a single shot.

**Sections**:
- Graph (pressure, flow, temperature, weight)
- Metrics row (duration, dose, output, ratio, rating)
- Bean info card
- Grinder card
- Analysis card (TDS, EY) - shown if data exists
- Notes card - shown if notes exist
- Debug log viewer (dialog)
- Delete button

**Properties**:
```qml
property int shotId: 0  // Set when navigating to page
```

### ShotComparisonPage

**Location**: `qml/pages/ShotComparisonPage.qml`

Side-by-side comparison of 2-3 shots.

**Features**:
- Color-coded legend (green, blue, orange)
- Overlaid graphs (pressure solid, flow dashed, weight dotted)
- Metrics comparison table

### Graph Components

**HistoryShotGraph** (`qml/components/HistoryShotGraph.qml`)
- Static graph for viewing historical shot data
- Properties: `pressureData`, `flowData`, `temperatureData`, `weightData`, `maxTime`

**ComparisonGraph** (`qml/components/ComparisonGraph.qml`)
- Multi-shot overlay graph
- Property: `comparisonModel` (ShotComparisonModel instance)
- 3 color-coded line sets for each shot

## Integration Points

### MainController

Shots are saved automatically in `onShotEnded()`:

```cpp
void MainController::onShotEnded() {
    // ... existing code ...

    // Stop debug logging
    QString debugLog;
    if (m_shotDebugLogger) {
        m_shotDebugLogger->stopCapture();
        debugLog = m_shotDebugLogger->getCapturedLog();
    }

    // Build metadata from Settings
    ShotMetadata metadata;
    metadata.beanBrand = m_settings->dyeBeanBrand();
    // ... all DYE fields ...

    // Save to local history (always)
    if (m_shotHistory && m_shotHistory->isReady()) {
        m_shotHistory->saveShot(m_shotDataModel, &m_currentProfile,
                                duration, finalWeight, doseWeight,
                                metadata, debugLog);
    }

    // ... visualizer upload code ...
}
```

Debug logging starts in `onEspressoCycleStarted()`:

```cpp
void MainController::onEspressoCycleStarted() {
    // ... existing code ...

    if (m_shotDebugLogger) {
        m_shotDebugLogger->startCapture();
        m_shotDebugLogger->logInfo(QString("Profile: %1").arg(m_currentProfile.title()));
    }
}
```

### QML Access

From any QML file:

```qml
// Access storage
MainController.shotHistory.getShots(0, 50)
MainController.shotHistory.totalShots

// Access comparison model
MainController.shotComparison.addShot(shotId)
MainController.shotComparison.shotCount
```

## User Interface Flow

```
IdlePage
    └── [History] button
            └── ShotHistoryPage
                    ├── [Filter/Search]
                    ├── [Shot List]
                    │       ├── Tap → Toggle selection
                    │       └── Long-press → ShotDetailPage
                    │                            ├── Graph
                    │                            ├── Metadata
                    │                            ├── [View Debug Log]
                    │                            └── [Delete]
                    └── [Compare] button (2+ selected)
                            └── ShotComparisonPage
                                    ├── Overlaid graphs
                                    └── Metrics table
```

## Performance Considerations

### Database
- **WAL mode** enabled for better concurrent access
- **Indexes** on all filter fields (timestamp, profile, bean, grinder, enjoyment)
- **Pagination**: Load 50 shots at a time
- **Lazy loading**: Time-series data only loaded when viewing specific shot

### Memory
- Compressed blobs: ~5-10KB per shot
- Comparison view: 3 shots × ~50KB = ~150KB (acceptable)
- List view only loads summaries, not time-series data

### Query Performance
Typical filter query on 500+ shots: <50ms on old tablets

## Data Retention

- **Default**: Unlimited (keep all shots forever)
- **Manual deletion**: Via shot detail page
- **No auto-purge**: User manages their own history

## Visualizer Integration

When a shot is uploaded to visualizer.coffee, the returned ID and URL are stored:

```cpp
shotHistory->updateVisualizerInfo(shotId, visualizerId, visualizerUrl);
```

This allows:
- Showing upload status in shot list (cloud icon)
- Linking to visualizer page from shot detail
- Avoiding duplicate uploads

## Bug Report Export

Export complete shot data for debugging:

```cpp
QString export = shotHistory->exportShotData(shotId);
```

Output format:
```
=== Decenza DE1 Shot Export ===
Shot ID: 12345
UUID: a1b2c3d4-...
Date: 2026-01-02T14:35:22

--- Profile ---
Name: Adaptive v2

--- Shot Metrics ---
Duration: 32.5s
Dose: 18.0g
Output: 36.2g
Ratio: 1:2.0

--- Bean Info ---
Brand: Counter Culture
Type: Ethiopian Yirgacheffe
Roast Date: 2025-12-15
Roast Level: Light

--- Grinder ---
Model: Niche Zero
Setting: 15

--- Analysis ---
TDS: 1.35%
EY: 21.2%
Enjoyment: 85%
Notes: Bright, citrus notes...
Barista: Alex

--- Debug Log ---
[0.000] START Shot capture started
...
[32.500] STOP Shot ended

--- Sample Data Summary ---
Pressure samples: 156
Flow samples: 156
Temperature samples: 156
Weight samples: 89
```

## Future Enhancements

Potential additions:
- Cloud sync/backup
- Shot sharing
- More advanced filtering (date ranges, ratio calculations)
- Statistical analysis (averages, trends)
- Profile suggestions based on history
- Batch export to CSV/JSON
