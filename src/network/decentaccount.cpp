#include "decentaccount.h"

#include "core/diagnosticlogging.h"
#include "core/settings_decent.h"
#include "network/httpauth.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
void openInBrowser(const QUrl& url) {
    if (!QDesktopServices::openUrl(url))
        DIAG_WARN(DECENT, "DecentAccount") << "no browser could open" << url.host();
}

// login_test's token is one short line; a captive portal's page is not.
bool looksLikeToken(const QString& body) {
    return body.size() <= 200 && !body.contains(QLatin1Char('<')) && !body.contains(QLatin1Char('\n'));
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
    request.setRawHeader("Authorization", basicAuthHeader(email, password));
    request.setTransferTimeout(kTransferTimeoutMs);
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
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: login_test returned HTTP" << status << reply->errorString();
        emit linkFinished(AccountLink::Error::ServerError);
        return;
    }
    // login_test answers 0 (or nothing) for bad credentials, otherwise the
    // encrypted password. Same acceptance test as Decaid's DecentAccountService.
    if (body.isEmpty() || body == QLatin1String("0")) {
        DIAG_INFO(DECENT, "DecentAccount") << "link rejected: email or password not accepted";
        emit linkFinished(AccountLink::Error::Rejected);
        return;
    }
    if (!looksLikeToken(body)) {
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: login_test answered 200 with" << body.size()
                                           << "characters that are not a token";
        emit linkFinished(AccountLink::Error::ServerError);
        return;
    }

    m_settings->setAccount(m_pendingEmail, body);
    // Connecting an account switches uploads to it on; the user can switch it off.
    m_settings->setEnabled(true);
    // No email: debug logs are submitted for support and readable over MCP.
    DIAG_INFO(DECENT, "DecentAccount") << "account linked";
    emit linkFinished(AccountLink::Error::None);
}

void DecentAccount::unlink() {
    // A sign-in still in flight must not re-link the account the user just disconnected.
    if (QNetworkReply* pending = m_linkReply) {
        m_linkReply = nullptr;
        disconnect(pending, nullptr, this, nullptr);
        pending->abort();
        pending->deleteLater();
        emit busyChanged();
    }
    if (!m_settings->linked() && m_settings->email().isEmpty()) return;
    m_settings->clearAccount();
    DIAG_INFO(DECENT, "DecentAccount") << "account unlinked";
}

bool DecentAccount::applyAuth(QNetworkRequest& request) const {
    // Credentials the server has already refused are not sent again.
    if (state() != State::Linked) return false;
    request.setRawHeader("Authorization", basicAuthHeader(m_settings->email(), m_settings->encryptedPassword()));
    return true;
}

void DecentAccount::reportAuthFailure() {
    if (!m_settings->linked() || m_settings->needsSignIn()) return;
    m_settings->setNeedsSignIn(true);
    DIAG_WARN(DECENT, "DecentAccount") << "the server rejected the stored credentials (HTTP 401); "
                                          "Decent uploads stop until the account is signed in again";
}

void DecentAccount::openAccountInBrowser() {
    const QUrl fallback(QString::fromLatin1(kBaseUrl) + QString::fromLatin1(kAccountPath));
    QNetworkRequest request(QUrl(QString::fromLatin1(kBaseUrl) + QStringLiteral("/support/api/authenticated_redirect")));
    if (!applyAuth(request)) {
        openInBrowser(fallback);
        return;
    }
    QUrl url = request.url();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("dest"), QString::fromLatin1(kAccountPath));
    url.setQuery(query);
    request.setUrl(url);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fallback]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) reportAuthFailure();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QUrl signedIn(json.value(QStringLiteral("url")).toString());
        if (status == 200 && json.value(QStringLiteral("ok")).toBool() && signedIn.isValid()
            && signedIn.host() == QUrl(fallback).host()) {
            openInBrowser(signedIn);
            return;
        }
        DIAG_INFO(DECENT, "DecentAccount") << "authenticated redirect unavailable (HTTP" << status
                                           << reply->errorString() << "); opening the account page unsigned";
        openInBrowser(fallback);
    });
}
