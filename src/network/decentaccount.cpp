#include "decentaccount.h"

#include "core/diagnosticlogging.h"
#include "core/settings_decent.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
// The page a signed-in owner's shots live on.
const QString kAccountPath = QStringLiteral("/support/espressomachine");

QByteArray basicAuth(const QString& user, const QString& secret) {
    return "Basic " + (user.trimmed() + QLatin1Char(':') + secret.trimmed()).toUtf8().toBase64();
}
}

DecentAccount::DecentAccount(QNetworkAccessManager* network, SettingsDecent* settings, QObject* parent)
    : QObject(parent)
    , m_network(network)
    , m_settings(settings)
{
    connect(m_settings, &SettingsDecent::accountChanged, this, &DecentAccount::stateChanged);
}

DecentAccount::State DecentAccount::state() const {
    if (!m_settings->linked()) return State::NotLinked;
    return m_settings->needsSignIn() ? State::NeedsSignIn : State::Linked;
}

QString DecentAccount::email() const {
    return m_settings->email();
}

void DecentAccount::link(const QString& email, const QString& password) {
    if (m_linkReply || email.trimmed().isEmpty() || password.isEmpty()) return;

    QNetworkRequest request(QUrl(QString::fromLatin1(kBaseUrl) + QStringLiteral("/support/api/login_test")));
    request.setRawHeader("Authorization", basicAuth(email, password));
    m_pendingEmail = email.trimmed();
    m_linkReply = m_network->get(request);
    connect(m_linkReply, &QNetworkReply::finished, this, &DecentAccount::onLinkFinished);
    emit busyChanged();
}

void DecentAccount::onLinkFinished() {
    QNetworkReply* reply = m_linkReply;
    m_linkReply = nullptr;
    if (!reply) return;
    reply->deleteLater();
    emit busyChanged();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString body = QString::fromUtf8(reply->readAll()).trimmed();

    if (reply->error() != QNetworkReply::NoError && status == 0) {
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: server unreachable:" << reply->errorString();
        emit linkFinished(AccountLink::Error::Unreachable);
        return;
    }
    if (status != 200) {
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: login_test returned HTTP" << status;
        emit linkFinished(AccountLink::Error::Unreachable);
        return;
    }
    // login_test answers 0 (or nothing) for bad credentials, otherwise the
    // encrypted password. Same acceptance test as Decaid's DecentAccountService.
    if (body.isEmpty() || body == QLatin1String("0")) {
        DIAG_INFO(DECENT, "DecentAccount") << "link rejected: email or password not accepted";
        emit linkFinished(AccountLink::Error::Rejected);
        return;
    }

    m_settings->setAccount(m_pendingEmail, body);
    // Connecting an account switches uploads to it on; the user can switch it off.
    m_settings->setEnabled(true);
    DIAG_INFO(DECENT, "DecentAccount") << "account linked:" << m_pendingEmail;
    emit linkFinished(AccountLink::Error::None);
}

void DecentAccount::unlink() {
    if (!m_settings->linked() && m_settings->email().isEmpty()) return;
    m_settings->clearAccount();
    DIAG_INFO(DECENT, "DecentAccount") << "account unlinked";
}

bool DecentAccount::applyAuth(QNetworkRequest& request) const {
    if (!m_settings->linked()) return false;
    request.setRawHeader("Authorization", basicAuth(m_settings->email(), m_settings->encryptedPassword()));
    return true;
}

void DecentAccount::reportAuthFailure() {
    if (!m_settings->linked() || m_settings->needsSignIn()) return;
    m_settings->setNeedsSignIn(true);
    DIAG_WARN(DECENT, "DecentAccount") << "the server rejected the stored credentials (HTTP 401); "
                                          "automatic uploads stop until the account is signed in again";
}

void DecentAccount::openAccountInBrowser() {
    const QUrl fallback(QString::fromLatin1(kBaseUrl) + kAccountPath);
    QNetworkRequest request(QUrl(QString::fromLatin1(kBaseUrl) + QStringLiteral("/support/api/authenticated_redirect")));
    if (!applyAuth(request)) {
        QDesktopServices::openUrl(fallback);
        return;
    }
    QUrl url = request.url();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("dest"), kAccountPath);
    url.setQuery(query);
    request.setUrl(url);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fallback]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) reportAuthFailure();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QUrl signedIn(json.value(QStringLiteral("url")).toString());
        if (status == 200 && json.value(QStringLiteral("ok")).toBool() && signedIn.isValid()
            && signedIn.host() == QUrl(fallback).host()) {
            QDesktopServices::openUrl(signedIn);
            return;
        }
        DIAG_INFO(DECENT, "DecentAccount") << "authenticated redirect unavailable (HTTP" << status
                                           << "); opening the account page unsigned";
        QDesktopServices::openUrl(fallback);
    });
}
