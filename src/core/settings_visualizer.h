#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>

// Visualizer (visualizer.coffee) upload settings.
class SettingsVisualizer : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString visualizerUsername READ visualizerUsername WRITE setVisualizerUsername NOTIFY visualizerUsernameChanged FINAL)
    Q_PROPERTY(QString visualizerPassword READ visualizerPassword WRITE setVisualizerPassword NOTIFY visualizerPasswordChanged FINAL)
    Q_PROPERTY(bool visualizerEnabled READ visualizerEnabled WRITE setVisualizerEnabled NOTIFY visualizerEnabledChanged FINAL)
    Q_PROPERTY(bool visualizerActive READ visualizerActive NOTIFY visualizerActiveChanged FINAL)
    Q_PROPERTY(bool visualizerExtendedMetadata READ visualizerExtendedMetadata WRITE setVisualizerExtendedMetadata NOTIFY visualizerExtendedMetadataChanged FINAL)
    Q_PROPERTY(bool visualizerShowAfterShot READ visualizerShowAfterShot WRITE setVisualizerShowAfterShot NOTIFY visualizerShowAfterShotChanged FINAL)
    Q_PROPERTY(bool visualizerClearNotesOnStart READ visualizerClearNotesOnStart WRITE setVisualizerClearNotesOnStart NOTIFY visualizerClearNotesOnStartChanged FINAL)

public:
    explicit SettingsVisualizer(QObject* parent = nullptr);

    QString visualizerUsername() const;
    void setVisualizerUsername(const QString& username);

    QString visualizerPassword() const;
    void setVisualizerPassword(const QString& password);

    // The Visualizer switch on the Shot Upload tab. When/what to upload is
    // SettingsUpload, shared with the Decent account.
    bool visualizerEnabled() const;
    void setVisualizerEnabled(bool enabled);
    // Switched on with both credentials set: shots go to Visualizer.
    bool visualizerActive() const;

    bool visualizerExtendedMetadata() const;
    void setVisualizerExtendedMetadata(bool enabled);

    bool visualizerShowAfterShot() const;
    void setVisualizerShowAfterShot(bool enabled);

    bool visualizerClearNotesOnStart() const;
    void setVisualizerClearNotesOnStart(bool enabled);

signals:
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerEnabledChanged();
    void visualizerActiveChanged();
    void visualizerExtendedMetadataChanged();
    void visualizerShowAfterShotChanged();
    void visualizerClearNotesOnStartChanged();

private:
    mutable AppSettings m_settings;
};
