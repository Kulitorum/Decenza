# Backup/Restore Refactoring Proposal

## Current Redundancies

### 1. exportDatabase() vs createBackup()

Both methods do nearly identical operations:

**exportDatabase():**
- Generates timestamp path in Downloads
- Close DB → QFile::copy → Reopen DB
- ❌ **Bug**: Doesn't work on Android (uses QFile::copy, not Java JNI)
- ❌ **Unused**: No callers found in codebase

**createBackup():**
- Takes destination path as parameter
- Checkpoint → Close DB → Platform-specific copy → Reopen DB
- ✅ Works on all platforms (Java on Android)
- ✅ Used by DatabaseBackupManager

## Recommendation

### Option 1: Delete exportDatabase() (SIMPLEST)
Since it's unused and broken on Android, just remove it.

### Option 2: Consolidate (BETTER)
Make exportDatabase() call createBackup():

```cpp
QString ShotHistoryStorage::exportDatabase()
{
    QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadsDir.isEmpty()) {
        downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString destPath = downloadsDir + "/shots_" + timestamp + ".db";

    return createBackup(destPath);  // Reuse existing logic
}
```

### Option 3: Extract Common Helper (MOST ROBUST)
Create a private helper method:

```cpp
private:
    // Core backup logic - closes DB, copies file, reopens DB
    bool performDatabaseCopy(const QString& destPath);
```

Then both exportDatabase() and createBackup() call it.

## Web Interface Considerations

The web `/api/backup/shots` endpoint currently does:
```cpp
m_storage->checkpoint();
sendFile(socket, m_storage->databasePath(), "application/x-sqlite3");
```

**Issue**: Doesn't close the database before sending. This is less safe than createBackup().

**Recommendation**: Consider having web interface use createBackup() to a temp file, then send that file. This ensures consistency.

## Summary

**Quick wins:**
1. Delete unused exportDatabase() method ← DO THIS
2. Make web backup endpoint more robust by closing DB before sending

**Long-term:**
- Extract common backup logic into helper method
- Ensure all backup paths use same safe approach (checkpoint + close + copy + reopen)
