#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class Settings;

// Client for the Loffee Labs Bean Base API (loffeelabs.com).
//
// This first cut implements only API-key validation (`testApiKey`), which is
// all the Settings UI needs for Tier 1. Search (`GET /beans`) with the
// debounce + 3 s rate-limit queue + response cache is added in Tier 2 when the
// BeanInfoPage search bar lands — the class is deliberately shaped to grow.
//
// The API key is read live from `Settings.beanbase.beanBaseApiKey` on each
// request, so a key change in the UI takes effect immediately without
// re-wiring. Free tier: 1 req / 3 s, 2,000 beans/day, 50 per call.
class BeanBaseClient : public QObject {
    Q_OBJECT

public:
    explicit BeanBaseClient(QNetworkAccessManager* networkManager,
                            Settings* settings, QObject* parent = nullptr);

    // Validates the currently-configured API key by issuing `GET /beans?limit=1`.
    // Emits apiKeyTestResult(success, message) when the response arrives.
    // A 200 → success; 401 → "Invalid API key"; transport failure → a reach error.
    Q_INVOKABLE void testApiKey();

signals:
    // Result of testApiKey(). `message` is a short, user-facing, translatable-key
    // fallback string; the QML layer maps status to a localized message.
    void apiKeyTestResult(bool success, const QString& message);

private:
    QString apiKey() const;

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    Settings* m_settings = nullptr;                     // Non-owning
};
