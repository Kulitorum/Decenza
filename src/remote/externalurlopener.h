#pragma once

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

// Reserves an external browser window during a user gesture and later routes a
// validated URL into that same window. Each QML page instance owns its own
// reservations so one client cannot redirect another client's browser.
class ExternalUrlOpener : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit ExternalUrlOpener(QObject* parent = nullptr);
    ~ExternalUrlOpener() override;

    Q_INVOKABLE QString reserve();
    Q_INVOKABLE bool openReserved(const QString& reservationId, const QString& url);
    Q_INVOKABLE void cancel(const QString& reservationId);

    static bool isSafeExternalUrl(const QUrl& url);

private:
    QSet<QString> m_reservations;
};
