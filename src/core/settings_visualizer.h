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
    Q_PROPERTY(bool visualizerConnected READ visualizerConnected NOTIFY visualizerActiveChanged FINAL)
    Q_PROPERTY(bool visualizerActive READ visualizerActive NOTIFY visualizerActiveChanged FINAL)
    Q_PROPERTY(bool visualizerExtendedMetadata READ visualizerExtendedMetadata WRITE setVisualizerExtendedMetadata NOTIFY visualizerExtendedMetadataChanged FINAL)
    Q_PROPERTY(bool visualizerShowAfterShot READ visualizerShowAfterShot WRITE setVisualizerShowAfterShot NOTIFY visualizerShowAfterShotChanged FINAL)
    Q_PROPERTY(bool visualizerClearNotesOnStart READ visualizerClearNotesOnStart WRITE setVisualizerClearNotesOnStart NOTIFY visualizerClearNotesOnStartChanged FINAL)

    // Bean Search community-shots lookup (CommunitySettingsClient). communityApiBaseUrl
    // points at the production API by default; set it to http://localhost:3000 to run
    // against a local Visualizer instance. communityMinEnjoyment floors both the ranking input and
    // the "Open on Visualizer" deep link.
    Q_PROPERTY(bool communitySearchEnabled READ communitySearchEnabled WRITE setCommunitySearchEnabled NOTIFY communitySearchEnabledChanged FINAL)
    Q_PROPERTY(int communityMinEnjoyment READ communityMinEnjoyment WRITE setCommunityMinEnjoyment NOTIFY communityMinEnjoymentChanged FINAL)
    Q_PROPERTY(QString communityApiBaseUrl READ communityApiBaseUrl WRITE setCommunityApiBaseUrl NOTIFY communityApiBaseUrlChanged FINAL)
    // DARK (default false): when on, the community card may express a grind
    // recorded on a DIFFERENT grinder, translated through per-grinder micron
    // scales (GrinderAliases::MicronScale). Off, grind shows only when the
    // community grinder matches the user's. Shipped off with no micron data
    // populated; see ADR-0020.
    Q_PROPERTY(bool communityRelativeGrind READ communityRelativeGrind WRITE setCommunityRelativeGrind NOTIFY communityRelativeGrindChanged FINAL)

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
    bool visualizerConnected() const;  // both credentials set
    bool visualizerActive() const;     // connected and switched on

    bool visualizerExtendedMetadata() const;
    void setVisualizerExtendedMetadata(bool enabled);

    bool visualizerShowAfterShot() const;
    void setVisualizerShowAfterShot(bool enabled);

    bool visualizerClearNotesOnStart() const;
    void setVisualizerClearNotesOnStart(bool enabled);

    bool communitySearchEnabled() const;
    void setCommunitySearchEnabled(bool enabled);

    int communityMinEnjoyment() const;
    void setCommunityMinEnjoyment(int value);

    QString communityApiBaseUrl() const;
    void setCommunityApiBaseUrl(const QString& url);

    bool communityRelativeGrind() const;
    void setCommunityRelativeGrind(bool enabled);

signals:
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerEnabledChanged();
    void visualizerActiveChanged();
    void visualizerExtendedMetadataChanged();
    void visualizerShowAfterShotChanged();
    void visualizerClearNotesOnStartChanged();
    void communitySearchEnabledChanged();
    void communityMinEnjoymentChanged();
    void communityApiBaseUrlChanged();
    void communityRelativeGrindChanged();

private:
    mutable AppSettings m_settings;
};
