#include "shotserver.h"
#include "webtemplates/auth_page.h"
#include "../core/settings.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSettings>
#include <QDateTime>

// ─── Base32 encoding/decoding ───────────────────────────────────────────────

static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static QString toBase32(const QByteArray& data)
{
    QString result;
    int bits = 0;
    int buffer = 0;
    for (int i = 0; i < data.size(); i++) {
        buffer = (buffer << 8) | static_cast<unsigned char>(data[i]);
        bits += 8;
        while (bits >= 5) {
            result.append(QLatin1Char(BASE32_ALPHABET[(buffer >> (bits - 5)) & 0x1F]));
            bits -= 5;
        }
    }
    if (bits > 0) {
        result.append(QLatin1Char(BASE32_ALPHABET[(buffer << (5 - bits)) & 0x1F]));
    }
    return result;
}

static QByteArray fromBase32(const QString& encoded)
{
    QByteArray result;
    int bits = 0;
    int buffer = 0;
    for (QChar c : encoded) {
        int val;
        char ch = c.toUpper().toLatin1();
        if (ch >= 'A' && ch <= 'Z') val = ch - 'A';
        else if (ch >= '2' && ch <= '7') val = ch - '2' + 26;
        else continue;
        buffer = (buffer << 5) | val;
        bits += 5;
        if (bits >= 8) {
            result.append(static_cast<char>((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

// ─── TOTP computation (RFC 6238) ────────────────────────────────────────────

static QString computeTotp(const QByteArray& secret, qint64 counter)
{
    // Convert counter to 8-byte big-endian
    QByteArray counterBytes(8, 0);
    for (int i = 7; i >= 0; i--) {
        counterBytes[i] = static_cast<char>(counter & 0xFF);
        counter >>= 8;
    }

    // HMAC-SHA1 using Qt
    QByteArray hmac = QMessageAuthenticationCode::hash(counterBytes, secret, QCryptographicHash::Sha1);

    // Dynamic truncation
    int offset = hmac[hmac.size() - 1] & 0x0F;
    quint32 code = (static_cast<quint32>(static_cast<unsigned char>(hmac[offset]) & 0x7F) << 24) |
                   (static_cast<quint32>(static_cast<unsigned char>(hmac[offset + 1]) & 0xFF) << 16) |
                   (static_cast<quint32>(static_cast<unsigned char>(hmac[offset + 2]) & 0xFF) << 8) |
                   static_cast<quint32>(static_cast<unsigned char>(hmac[offset + 3]) & 0xFF);

    code = code % 1000000;
    return QString("%1").arg(code, 6, 10, QChar('0'));
}

static bool validateTotp(const QByteArray& secret, const QString& code)
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 counter = now / 30;

    // Check current + ±2 time steps (150-second window for clock drift)
    for (int i = -2; i <= 2; i++) {
        if (computeTotp(secret, counter + i) == code) {
            return true;
        }
    }
    return false;
}

// ─── Helper: extract User-Agent from request ────────────────────────────────

static QString extractUserAgent(const QByteArray& request)
{
    QString requestStr = QString::fromUtf8(request);
    QStringList lines = requestStr.split("\r\n");
    for (const QString& line : lines) {
        if (line.startsWith("User-Agent:", Qt::CaseInsensitive)) {
            return line.mid(11).trimmed();
        }
    }
    return QString();
}

// ─── Session token hashing ──────────────────────────────────────────────────

static QString hashToken(const QString& token)
{
    return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
}

// ─── Rate limiting ──────────────────────────────────────────────────────────

bool ShotServer::checkRateLimit(const QString& ip)
{
    auto now = QDateTime::currentDateTimeUtc();

    // Periodic cleanup: evict stale entries to prevent unbounded memory growth
    if (m_loginAttempts.size() > 100) {
        QMutableHashIterator<QString, QPair<int, QDateTime>> cleanup(m_loginAttempts);
        while (cleanup.hasNext()) {
            cleanup.next();
            if (cleanup.value().second.secsTo(now) > 60)
                cleanup.remove();
        }
    }

    auto it = m_loginAttempts.find(ip);
    if (it != m_loginAttempts.end()) {
        // Reset window if older than 60 seconds
        if (it->second.secsTo(now) > 60) {
            it->first = 0;
            it->second = now;
        }
        if (it->first >= 5) {
            return false;  // Rate limited
        }
        it->first++;
    } else {
        m_loginAttempts[ip] = {1, now};
    }
    return true;
}

// ─── QML-callable TOTP setup methods ────────────────────────────────────────

QVariantMap ShotServer::generateTotpSetup()
{
    // Generate random 20-byte secret using OS CSPRNG
    QByteArray secretBytes(20, 0);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(secretBytes.data()),
        secretBytes.size() / static_cast<int>(sizeof(quint32)));

    QString base32Secret = toBase32(secretBytes);
    QString uri = QString("otpauth://totp/Decenza:DE1?secret=%1&issuer=Decenza&algorithm=SHA1&digits=6&period=30")
                      .arg(base32Secret);

    QVariantMap result;
    result["secret"] = base32Secret;
    result["uri"] = uri;
    return result;
}

bool ShotServer::completeTotpSetup(const QString& secret, const QString& code)
{
    QByteArray secretBytes = fromBase32(secret);
    if (secretBytes.isEmpty()) {
        return false;
    }

    if (!validateTotp(secretBytes, code)) {
        qDebug() << "ShotServer: TOTP setup verification failed";
        return false;
    }

    // Store the verified secret
    if (m_settings) {
        m_settings->setValue("webAuth/totpSecret", secret);
        m_settings->setValue("webAuth/credentialId", QVariant());
        m_settings->setValue("webAuth/credentialPublicKey", QVariant());
        m_settings->sync();
    }

    emit hasTotpSecretChanged();
    return true;
}

void ShotServer::resetTotpSecret()
{
    if (m_settings) {
        m_settings->setValue("webAuth/totpSecret", QVariant());
        m_settings->sync();
    }
    m_sessions.clear();
    saveSessions();
    qDebug() << "ShotServer: TOTP secret and all sessions cleared";
    emit hasTotpSecretChanged();
}

// ─── Web auth route handler ─────────────────────────────────────────────────

void ShotServer::handleAuthRoute(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body)
{
    if (path == "/auth/login" && method == "GET") {
        if (hasStoredTotpSecret()) {
            sendResponse(socket, 200, "text/html; charset=utf-8", QByteArray(WEB_AUTH_LOGIN_PAGE));
        } else {
            sendResponse(socket, 200, "text/html; charset=utf-8", QByteArray(WEB_AUTH_SETUP_REQUIRED_PAGE));
        }
    }
    else if (path == "/auth/setup-required" && method == "GET") {
        sendResponse(socket, 200, "text/html; charset=utf-8", QByteArray(WEB_AUTH_SETUP_REQUIRED_PAGE));
    }
    else if (path == "/api/auth/login" && method == "POST") {
        handleTotpLogin(socket, body);
    }
    else if (path == "/api/auth/reset" && method == "POST") {
        // Require valid session to reset
        QByteArray fullRequest = socket->property("fullRequest").toByteArray();
        if (!checkSession(fullRequest)) {
            sendResponse(socket, 401, "application/json", R"({"error":"Unauthorized"})");
            return;
        }
        resetTotpSecret();
        sendJson(socket, R"({"success":true})");
    }
    else {
        sendResponse(socket, 404, "text/plain", "Not Found");
    }
}

// ─── TOTP login handler ─────────────────────────────────────────────────────

void ShotServer::handleTotpLogin(QTcpSocket* socket, const QByteArray& body)
{
    // Rate limiting by IP
    QString clientIp = (socket->state() != QAbstractSocket::UnconnectedState)
        ? socket->peerAddress().toString() : "unknown";
    if (!checkRateLimit(clientIp)) {
        sendResponse(socket, 429, "application/json",
                     R"({"error":"Too many attempts. Please wait 60 seconds."})");
        return;
    }

    if (!hasStoredTotpSecret()) {
        sendResponse(socket, 400, "application/json",
                     R"({"error":"TOTP not configured. Set up in the Decenza app first."})");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid JSON"})");
        return;
    }

    QString code = doc.object()["code"].toString().trimmed();
    if (code.length() != 6) {
        sendResponse(socket, 400, "application/json", R"({"error":"Code must be 6 digits"})");
        return;
    }

    // Validate TOTP
    QString storedSecret = m_settings->value("webAuth/totpSecret").toString();
    QByteArray secretBytes = fromBase32(storedSecret);

    if (!validateTotp(secretBytes, code)) {
        sendResponse(socket, 401, "application/json", R"({"error":"Invalid code"})");
        return;
    }

    // Reset rate limit on success
    m_loginAttempts.remove(clientIp);

    // Create session
    QByteArray fullRequest = socket->property("fullRequest").toByteArray();
    QString userAgent = extractUserAgent(fullRequest);
    QString token = createSession(userAgent);

    int maxAge = SESSION_LIFETIME_DAYS * 24 * 60 * 60;
    QString cookie = QString("decenza_session=%1; Max-Age=%2; Path=/; Secure; HttpOnly; SameSite=Lax")
                         .arg(token).arg(maxAge);

    QByteArray responseBody = R"({"success":true})";
    QByteArray extraHeaders = QString("Set-Cookie: %1\r\n").arg(cookie).toUtf8();
    sendResponse(socket, 200, "application/json", responseBody, extraHeaders);
}

// ─── Session management (unchanged from WebAuthn version) ───────────────────

QString ShotServer::extractCookie(const QByteArray& request, const QString& cookieName) const
{
    QString requestStr = QString::fromUtf8(request);
    QStringList lines = requestStr.split("\r\n");
    for (const QString& line : lines) {
        if (line.startsWith("Cookie:", Qt::CaseInsensitive)) {
            QString cookieStr = line.mid(7).trimmed();
            QStringList cookies = cookieStr.split(";");
            for (const QString& cookie : cookies) {
                QString trimmed = cookie.trimmed();
                if (trimmed.startsWith(cookieName + "=")) {
                    return trimmed.mid(cookieName.length() + 1);
                }
            }
        }
    }
    return QString();
}

bool ShotServer::checkSession(const QByteArray& request) const
{
    QString token = extractCookie(request, "decenza_session");
    if (token.isEmpty()) return false;

    QString hashed = hashToken(token);
    auto it = m_sessions.find(hashed);
    if (it == m_sessions.end()) return false;

    return it->expiry > QDateTime::currentDateTimeUtc();
}

QString ShotServer::createSession(const QString& userAgent)
{
    QByteArray tokenBytes(32, 0);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(tokenBytes.data()),
        tokenBytes.size() / static_cast<int>(sizeof(quint32)));
    QString token = tokenBytes.toHex();

    SessionInfo info;
    info.expiry = QDateTime::currentDateTimeUtc().addDays(SESSION_LIFETIME_DAYS);
    info.userAgent = userAgent;
    m_sessions[hashToken(token)] = info;  // Store hashed, return plaintext for cookie

    saveSessions();
    return token;
}

bool ShotServer::hasStoredTotpSecret() const
{
    if (!m_settings) return false;
    QVariant secret = m_settings->value("webAuth/totpSecret");
    return secret.isValid() && !secret.toString().isEmpty();
}

void ShotServer::loadSessions()
{
    if (!m_settings) return;
    QSettings settings;
    int count = settings.beginReadArray("webAuth/sessions");
    for (int i = 0; i < count; i++) {
        settings.setArrayIndex(i);
        QString tokenOrHash = settings.value("token").toString();
        SessionInfo info;
        info.expiry = settings.value("expiry").toDateTime();
        info.userAgent = settings.value("userAgent").toString();
        if (info.expiry > QDateTime::currentDateTimeUtc()) {
            // Store with hash as key (old plaintext tokens get hashed on next save)
            QString key = (tokenOrHash.length() == 64) ? tokenOrHash : hashToken(tokenOrHash);
            m_sessions[key] = info;
        }
    }
    settings.endArray();
    qDebug() << "ShotServer: Loaded" << m_sessions.size() << "active sessions";
}

void ShotServer::saveSessions()
{
    QSettings settings;
    // Clear expired sessions first
    QMutableHashIterator<QString, SessionInfo> it(m_sessions);
    while (it.hasNext()) {
        it.next();
        if (it.value().expiry <= QDateTime::currentDateTimeUtc()) {
            it.remove();
        }
    }

    settings.beginWriteArray("webAuth/sessions", m_sessions.size());
    int i = 0;
    for (auto sit = m_sessions.constBegin(); sit != m_sessions.constEnd(); ++sit, ++i) {
        settings.setArrayIndex(i);
        settings.setValue("token", sit.key());  // Already hashed
        settings.setValue("expiry", sit.value().expiry);
        settings.setValue("userAgent", sit.value().userAgent);
    }
    settings.endArray();
}
