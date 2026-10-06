#pragma once

#include <QObject>
#include "appsettings.h"

// When and what to upload automatically, shared by the upload destinations; each
// has only its own switch and account. ShotUploads applies it for both.
class SettingsUpload : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool autoUpload READ autoUpload WRITE setAutoUpload NOTIFY autoUploadChanged FINAL)
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY autoUpdateChanged FINAL)
    Q_PROPERTY(double minDuration READ minDuration WRITE setMinDuration NOTIFY minDurationChanged FINAL)

public:
    explicit SettingsUpload(QObject* parent = nullptr);

    bool autoUpload() const;
    void setAutoUpload(bool enabled);

    bool autoUpdate() const;
    void setAutoUpdate(bool enabled);

    double minDuration() const;
    void setMinDuration(double seconds);

    // When this device's unfinished Upload missing shots run for `destination`
    // started, in seconds since the epoch; 0 when there is none. Device state, so
    // settings export leaves it out.
    qint64 missingRunStartedAt(const QString& destination) const;
    void setMissingRunStartedAt(const QString& destination, qint64 secsSinceEpoch);

    // Until when `destination`'s server asked for no more requests (HTTP 429), in
    // ms since the epoch; 0 when it has not. Every sender to it waits until then.
    // Device state, so settings export leaves it out.
    qint64 rateLimitedUntilMs(const QString& destination) const;
    void setRateLimitedUntilMs(const QString& destination, qint64 msSinceEpoch);
    // How long the wait has left, in ms; 0 when there is none. The one test every sender uses.
    qint64 rateLimitRemainingMs(const QString& destination) const;
    // A 429 from `destination`: no requests for kRateLimitWaitMs from now. A 429
    // during a wait does not extend it: the server's window is fixed from its
    // first request (Solid Cache's increment keeps the entry's expiry, store/api.rb).
    void noteRateLimited(const QString& destination);
    // Visualizer allows 200 API requests per user in 10 minutes and sends no
    // Retry-After (api/base_controller.rb:3-14), so a 429 waits out the window.
    static constexpr qint64 kRateLimitWaitMs = 10 * 60 * 1000;

signals:
    void autoUploadChanged();
    void autoUpdateChanged();
    void minDurationChanged();
    void rateLimitedUntilChanged(const QString& destination);

private:
    mutable AppSettings m_settings;
};
