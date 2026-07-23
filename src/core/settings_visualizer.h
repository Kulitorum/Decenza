#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>

// Visualizer (visualizer.coffee) upload settings.
class SettingsVisualizer : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString visualizerUsername READ visualizerUsername WRITE setVisualizerUsername NOTIFY visualizerUsernameChanged)
    Q_PROPERTY(QString visualizerPassword READ visualizerPassword WRITE setVisualizerPassword NOTIFY visualizerPasswordChanged)
    Q_PROPERTY(bool visualizerAutoUpload READ visualizerAutoUpload WRITE setVisualizerAutoUpload NOTIFY visualizerAutoUploadChanged)
    Q_PROPERTY(bool visualizerAutoUpdate READ visualizerAutoUpdate WRITE setVisualizerAutoUpdate NOTIFY visualizerAutoUpdateChanged)
    Q_PROPERTY(double visualizerMinDuration READ visualizerMinDuration WRITE setVisualizerMinDuration NOTIFY visualizerMinDurationChanged)
    Q_PROPERTY(bool visualizerExtendedMetadata READ visualizerExtendedMetadata WRITE setVisualizerExtendedMetadata NOTIFY visualizerExtendedMetadataChanged)
    Q_PROPERTY(bool visualizerShowAfterShot READ visualizerShowAfterShot WRITE setVisualizerShowAfterShot NOTIFY visualizerShowAfterShotChanged)
    Q_PROPERTY(bool visualizerClearNotesOnStart READ visualizerClearNotesOnStart WRITE setVisualizerClearNotesOnStart NOTIFY visualizerClearNotesOnStartChanged)

public:
    explicit SettingsVisualizer(QObject* parent = nullptr);

    QString visualizerUsername() const;
    void setVisualizerUsername(const QString& username);

    QString visualizerPassword() const;
    void setVisualizerPassword(const QString& password);

    bool visualizerAutoUpload() const;
    void setVisualizerAutoUpload(bool enabled);

    bool visualizerAutoUpdate() const;
    void setVisualizerAutoUpdate(bool enabled);

    double visualizerMinDuration() const;
    void setVisualizerMinDuration(double seconds);

    bool visualizerExtendedMetadata() const;
    void setVisualizerExtendedMetadata(bool enabled);

    bool visualizerShowAfterShot() const;
    void setVisualizerShowAfterShot(bool enabled);

    bool visualizerClearNotesOnStart() const;
    void setVisualizerClearNotesOnStart(bool enabled);

signals:
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerAutoUploadChanged();
    void visualizerAutoUpdateChanged();
    void visualizerMinDurationChanged();
    void visualizerExtendedMetadataChanged();
    void visualizerShowAfterShotChanged();
    void visualizerClearNotesOnStartChanged();

private:
    mutable AppSettings m_settings;
};
