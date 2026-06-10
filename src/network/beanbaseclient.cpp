#include "beanbaseclient.h"

#include "../core/settings.h"
#include "../core/settings_beanbase.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
// Confirmed June 2026 by probing the public endpoints — see design.md.
// `/beans` requires auth; `/roasters|/origins|/varieties|/processes` are public.
constexpr auto kBeanBaseBaseUrl = "https://loffeelabs.com/api/v2";
}  // namespace

BeanBaseClient::BeanBaseClient(QNetworkAccessManager* networkManager,
                               Settings* settings, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_settings(settings)
{
}

QString BeanBaseClient::apiKey() const {
    return m_settings ? m_settings->beanbase()->beanBaseApiKey() : QString();
}

void BeanBaseClient::testApiKey() {
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit apiKeyTestResult(false, QStringLiteral("missing"));
        return;
    }
    if (!m_networkManager) {
        emit apiKeyTestResult(false, QStringLiteral("network"));
        return;
    }

    // A 1-bean fetch is the cheapest authenticated call; 200 proves the key works.
    QNetworkRequest request{QUrl(QStringLiteral("%1/beans?limit=1").arg(kBeanBaseBaseUrl))};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            emit apiKeyTestResult(true, QStringLiteral("success"));
        } else if (status == 401) {
            emit apiKeyTestResult(false, QStringLiteral("invalid"));
        } else if (status == 429) {
            emit apiKeyTestResult(false, QStringLiteral("ratelimited"));
        } else {
            // Transport failure or unexpected status — treat as "couldn't reach".
            emit apiKeyTestResult(false, QStringLiteral("network"));
        }
    });
}
