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

signals:
    void autoUploadChanged();
    void autoUpdateChanged();
    void minDurationChanged();

private:
    mutable AppSettings m_settings;
};
