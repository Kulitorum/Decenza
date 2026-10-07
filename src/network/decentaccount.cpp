#include "decentaccount.h"

#include "core/diagnosticlogging.h"
#include "core/settings_decent.h"
#include "network/httpauth.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {
void openInBrowser(const QUrl& url) {
    if (!QDesktopServices::openUrl(url))
        DIAG_WARN(DECENT, "DecentAccount") << "no browser could open" << url.host();
}

// How a request ended, for the log: time taken, HTTP status, Qt's error code.
QString replyOutcome(const QNetworkReply* reply, const QElapsedTimer& timer) {
    return QStringLiteral("after %1 ms, HTTP %2, %3")
        .arg(timer.elapsed())
        .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
        .arg(QString::fromLatin1(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(reply->error())));
}

struct RegisteredDe1 {
    QString serial;
    QString sku;
    int model;
};

// The DE1s among /support/api/sn's "serial sku" lines, each serial once.
QList<RegisteredDe1> registeredDe1s(const QStringList& machines) {
    QList<RegisteredDe1> de1s;
    for (const QString& line : machines) {
        const QStringList parts = line.simplified().split(QLatin1Char(' '));
        const QString serial = parts.value(0);
        const QString sku = parts.value(1);
        const int model = DecentAccount::skuModel(sku);
        if (serial.isEmpty() || model == 0) continue;
        if (std::any_of(de1s.cbegin(), de1s.cend(), [&](const RegisteredDe1& d) { return d.serial == serial; })) continue;
        de1s.append({serial, sku, model});
    }
    return de1s;
}

// login_test's token is one short line; a captive portal's page is not.
bool looksLikeToken(const QString& body) {
    return body.size() <= 200 && !body.contains(QLatin1Char('<')) && !body.contains(QLatin1Char('{'))
        && !body.contains(QLatin1Char('\n'));
}
}

DecentAccount::DecentAccount(QNetworkAccessManager* network, SettingsDecent* settings, QObject* parent)
    : QObject(parent)
    , m_network(network)
    , m_settings(settings)
{
    connect(m_settings, &SettingsDecent::accountChanged, this, &DecentAccount::stateChanged);
    connect(m_settings, &SettingsDecent::machinesChanged, this, &DecentAccount::machinesChanged);
}

DecentAccount::State DecentAccount::state() const {
    if (!m_settings->linked()) return State::NotLinked;
    return m_settings->needsSignIn() ? State::NeedsSignIn : State::Linked;
}

bool DecentAccount::uploadsActive() const {
    return m_settings->active();
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
    m_linkTimer.start();
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
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: server unreachable" << replyOutcome(reply, m_linkTimer)
                                           << reply->errorString();
        emit linkFinished(AccountLink::Error::Unreachable);
        return;
    }
    if (status != 200) {
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: login_test" << replyOutcome(reply, m_linkTimer)
                                           << reply->errorString();
        emit linkFinished(AccountLink::Error::ServerError);
        return;
    }
    // login_test answers 0 (or nothing) for bad credentials, otherwise the
    // encrypted password. Same acceptance test as Decaid's DecentAccountService.
    if (body.isEmpty() || body == QLatin1String("0")) {
        DIAG_INFO(DECENT, "DecentAccount") << "link rejected: email or password not accepted"
                                           << replyOutcome(reply, m_linkTimer);
        emit linkFinished(AccountLink::Error::Rejected);
        return;
    }
    if (!looksLikeToken(body)) {
        DIAG_WARN(DECENT, "DecentAccount") << "link failed: login_test answered" << body.size()
                                           << "characters that are not a token" << replyOutcome(reply, m_linkTimer);
        emit linkFinished(AccountLink::Error::ServerError);
        return;
    }

    // setAccount clears the previous machine list; nothing is noted about it until the new one is read.
    m_unreportedSerialNoted = true;
    m_settings->setAccount(m_pendingEmail, body);
    // Connecting an account switches uploads to it on; the user can switch it off.
    m_settings->setEnabled(true);
    // No email: debug logs are submitted for support and readable over MCP.
    DIAG_INFO(DECENT, "DecentAccount") << "account linked" << replyOutcome(reply, m_linkTimer);
    emit linkFinished(AccountLink::Error::None);
    fetchMachines();
    m_unreportedSerialNoted = false;
}

void DecentAccount::fetchMachines() {
    QNetworkRequest request(QUrl(QString::fromLatin1(kBaseUrl)
                                 + QStringLiteral("/support/api/sn?onlyespressomachines=1&withskus=1")));
    if (!applyAuth(request)) return;
    request.setTransferTimeout(kTransferTimeoutMs);
    if (m_machinesReply) {
        disconnect(m_machinesReply, nullptr, this, nullptr);
        m_machinesReply->abort();
        m_machinesReply->deleteLater();
    }
    m_machinesTimer.start();
    QNetworkReply* reply = m_network->get(request);
    m_machinesReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onMachinesFinished(reply); });
}

void DecentAccount::onMachinesFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (m_machinesReply != reply) return;
    m_machinesReply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) reportAuthFailure();
    const QString body = QString::fromUtf8(reply->readAll()).trimmed();
    // "0" is the API's refusal; markup is a captive portal or a proxy.
    if (status != 200 || body == QLatin1String("0") || body.contains(QLatin1Char('<'))) {
        DIAG_WARN(DECENT, "DecentAccount") << "could not read the account's machines" << replyOutcome(reply, m_machinesTimer)
                                           << "- a DE1 that reports no serial number cannot upload until the account is "
                                              "signed in again";
        return;
    }
    QStringList lines;
    for (const QString& line : body.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) lines.append(line.trimmed());
    }
    m_settings->setRegisteredMachines(lines);
    DIAG_INFO(DECENT, "DecentAccount") << "account lists" << lines.size() << "espresso machine(s),"
                                       << registeredDe1s(lines).size() << "of them DE1s"
                                       << replyOutcome(reply, m_machinesTimer);
}

int DecentAccount::skuModel(const QString& sku) {
    // Decaid's parseSkuModel (registered_decent_machine.dart): the longest token
    // first, matched only at a token boundary. A Bengle (BE1BENGLE) is not a DE1.
    static const std::pair<const char*, int> kTokens[] = {
        {"DE1XXXL", 7}, {"DE1XXL", 6}, {"DE1XL", 4}, {"DE1CAFE", 5}, {"DE1PRO", 3},
        {"DE1PLUS", 2}, {"DE1+", 2}, {"BE1BENGLE", 0}, {"DE1", 1},
    };
    const QString upper = sku.toUpper();
    if (!upper.startsWith(QLatin1String("DE-"))) return 0;
    const QString model = upper.mid(3);
    for (const auto& [token, value] : kTokens) {
        const QLatin1String t(token);
        if (!model.startsWith(t)) continue;
        const bool boundary = model.size() == t.size() || !model.at(t.size()).isUpper();
        return boundary ? value : 0;
    }
    return 0;
}

DecentAccount::UnreportedSerial DecentAccount::resolveUnreportedSerial(const QStringList& machines, int machineModel,
                                                                       const QString& chosen) {
    const QList<RegisteredDe1> de1s = registeredDe1s(machines);
    UnreportedSerial out;
    const auto settle = [&](const QString& serial) { out.serial = serial; return out; };
    if (!chosen.isEmpty() && std::any_of(de1s.cbegin(), de1s.cend(), [&](const RegisteredDe1& d) { return d.serial == chosen; }))
        return settle(chosen);
    if (de1s.size() == 1) return settle(de1s.first().serial);
    if (machineModel > 0) {
        QStringList ofModel;
        for (const RegisteredDe1& d : de1s) if (d.model == machineModel) ofModel.append(d.serial);
        if (ofModel.size() == 1) return settle(ofModel.first());
    }
    for (const RegisteredDe1& d : de1s) {
        out.choices.append(d.serial);
        out.labels.append(QStringLiteral("%1 · %2").arg(d.serial, d.sku));
    }
    return out;
}

QString DecentAccount::serialForUnreportedMachine(int machineModel) const {
    return resolveUnreportedSerial(m_settings->registeredMachines(), machineModel, m_settings->chosenMachine()).serial;
}

void DecentAccount::machineReportsNoSerial(int machineModel) {
    if (state() != State::Linked || m_unreportedSerialNoted || m_machinesReply) return;
    m_unreportedSerialNoted = true;
    const QStringList machines = m_settings->registeredMachines();
    const UnreportedSerial r = resolveUnreportedSerial(machines, machineModel, m_settings->chosenMachine());
    if (!r.serial.isEmpty()) {
        DIAG_INFO(DECENT, "DecentAccount") << "the DE1 reports no serial number; its shots go up under the account's DE1"
                                           << r.serial;
    } else if (!r.choices.isEmpty()) {
        DIAG_INFO(DECENT, "DecentAccount") << "the DE1 reports no serial number and the account has" << r.choices.size()
                                           << "DE1s; asking which one it is";
        emit machineChoiceNeeded(r.choices, r.labels);
    } else if (machines.isEmpty()) {
        // Accounts linked before the list was read at sign-in have none.
        DIAG_WARN(DECENT, "DecentAccount") << "the DE1 reports no serial number and the account's machines have not been "
                                              "read - its shots cannot be uploaded until the Decent account is signed out "
                                              "and signed in again";
    } else {
        DIAG_WARN(DECENT, "DecentAccount") << "the DE1 reports no serial number and the account lists no DE1 to file its "
                                              "shots under - they cannot be uploaded";
    }
}

void DecentAccount::chooseMachine(const QString& serial) {
    m_settings->setChosenMachine(serial);
    DIAG_INFO(DECENT, "DecentAccount") << "the user chose DE1" << serial << "for the machine that reports no serial number";
}

void DecentAccount::unlink() {
    // A sign-in still in flight must not re-link the account the user just disconnected.
    if (QNetworkReply* pending = m_linkReply) {
        m_linkReply = nullptr;
        disconnect(pending, nullptr, this, nullptr);
        pending->abort();
        pending->deleteLater();
        emit busyChanged();
        DIAG_INFO(DECENT, "DecentAccount") << "sign-in cancelled after" << m_linkTimer.elapsed() << "ms";
        emit linkFinished(AccountLink::Error::Cancelled);
    }
    if (QNetworkReply* pending = m_machinesReply) {
        m_machinesReply = nullptr;
        disconnect(pending, nullptr, this, nullptr);
        pending->abort();
        pending->deleteLater();
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

    QElapsedTimer timer;
    timer.start();
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fallback, timer]() {
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
        DIAG_INFO(DECENT, "DecentAccount") << "authenticated redirect unavailable" << replyOutcome(reply, timer)
                                           << reply->errorString() << "- opening the account page unsigned";
        openInBrowser(fallback);
    });
}
