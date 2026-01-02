#include "shotserver.h"
#include "webdebuglogger.h"
#include "../history/shothistorystorage.h"

#include <QNetworkInterface>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

ShotServer::ShotServer(ShotHistoryStorage* storage, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
{
}

ShotServer::~ShotServer()
{
    stop();
}

QString ShotServer::url() const
{
    if (!isRunning()) return QString();
    return QString("http://%1:%2").arg(getLocalIpAddress()).arg(m_port);
}

void ShotServer::setPort(int port)
{
    if (m_port != port) {
        m_port = port;
        emit portChanged();
    }
}

bool ShotServer::start()
{
    if (m_server) {
        stop();
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ShotServer::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, m_port)) {
        qWarning() << "ShotServer: Failed to start on port" << m_port << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    qDebug() << "ShotServer: Started on" << url();
    emit runningChanged();
    emit urlChanged();
    return true;
}

void ShotServer::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        emit runningChanged();
        emit urlChanged();
        qDebug() << "ShotServer: Stopped";
    }
}

void ShotServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ShotServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ShotServer::onDisconnected);
        emit clientConnected(socket->peerAddress().toString());
    }
}

void ShotServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Accumulate data
    PendingRequest& pending = m_pendingRequests[socket];
    pending.data.append(socket->readAll());

    // Find header end if not found yet
    if (pending.headerEnd < 0) {
        pending.headerEnd = pending.data.indexOf("\r\n\r\n");
        if (pending.headerEnd < 0) {
            // Headers not complete yet
            return;
        }

        // Parse Content-Length from headers
        QString headers = QString::fromUtf8(pending.data.left(pending.headerEnd));
        for (const QString& line : headers.split("\r\n")) {
            if (line.startsWith("Content-Length:", Qt::CaseInsensitive)) {
                pending.contentLength = line.mid(15).trimmed().toLongLong();
                break;
            }
        }

        // No content-length means no body expected
        if (pending.contentLength < 0) {
            pending.contentLength = 0;
        }
    }

    // Check if we have all the body data
    qint64 bodyStart = pending.headerEnd + 4;
    qint64 bodyReceived = pending.data.size() - bodyStart;

    if (bodyReceived < pending.contentLength) {
        // Still waiting for more data
        if (pending.contentLength > 1024 * 1024) {  // Log progress for large uploads
            static qint64 lastLog = 0;
            if (bodyReceived - lastLog > 1024 * 1024) {
                qDebug() << "Upload progress:" << bodyReceived / (1024*1024) << "MB /" << pending.contentLength / (1024*1024) << "MB";
                lastLog = bodyReceived;
            }
        }
        return;
    }

    // Request complete, handle it
    QByteArray request = pending.data;
    m_pendingRequests.remove(socket);
    handleRequest(socket, request);
}

void ShotServer::onDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        m_pendingRequests.remove(socket);
        socket->deleteLater();
    }
}

void ShotServer::handleRequest(QTcpSocket* socket, const QByteArray& request)
{
    QString requestStr = QString::fromUtf8(request);
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) {
        socket->close();
        return;
    }

    QStringList requestLine = lines.first().split(" ");
    if (requestLine.size() < 2) {
        socket->close();
        return;
    }

    QString method = requestLine[0];
    QString path = requestLine[1];

    qDebug() << "ShotServer:" << method << path;

    // Route requests
    if (path == "/" || path == "/index.html") {
        sendHtml(socket, generateShotListPage());
    }
    else if (path == "/shots" || path == "/shots/") {
        sendHtml(socket, generateShotListPage());
    }
    else if (path.startsWith("/compare/")) {
        // /compare/1,2,3 - compare shots with IDs 1, 2, 3
        QString idsStr = path.mid(9);
        QStringList idParts = idsStr.split(",");
        QList<qint64> ids;
        for (const QString& p : idParts) {
            bool ok;
            qint64 id = p.toLongLong(&ok);
            if (ok) ids << id;
        }
        if (ids.size() >= 2) {
            sendHtml(socket, generateComparisonPage(ids));
        } else {
            sendResponse(socket, 400, "text/plain", "Need at least 2 shot IDs to compare");
        }
    }
    else if (path.startsWith("/shot/")) {
        bool ok;
        qint64 shotId = path.mid(6).split("?").first().toLongLong(&ok);
        if (ok) {
            sendHtml(socket, generateShotDetailPage(shotId));
        } else {
            sendResponse(socket, 400, "text/plain", "Invalid shot ID");
        }
    }
    else if (path == "/api/shots") {
        QVariantList shots = m_storage->getShots(0, 1000);
        QJsonArray arr;
        for (const QVariant& v : shots) {
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        }
        sendJson(socket, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
    else if (path.startsWith("/api/shot/")) {
        bool ok;
        qint64 shotId = path.mid(10).toLongLong(&ok);
        if (ok) {
            QVariantMap shot = m_storage->getShot(shotId);
            sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(shot)).toJson());
        } else {
            sendResponse(socket, 400, "application/json", R"({"error":"Invalid shot ID"})");
        }
    }
    else if (path == "/api/database" || path == "/database.db") {
        QString dbPath = m_storage->databasePath();
        sendFile(socket, dbPath, "application/x-sqlite3");
    }
    else if (path == "/debug") {
        sendHtml(socket, generateDebugPage());
    }
    else if (path == "/api/debug" || path.startsWith("/api/debug?")) {
        // Get afterIndex from query string
        int afterIndex = 0;
        if (path.contains("?")) {
            QUrlQuery query(path.mid(path.indexOf("?") + 1));
            afterIndex = query.queryItemValue("after").toInt();
        }

        int lastIndex = 0;
        QStringList lines;
        if (WebDebugLogger::instance()) {
            lines = WebDebugLogger::instance()->getLines(afterIndex, &lastIndex);
        }

        QJsonObject result;
        result["lastIndex"] = lastIndex;
        QJsonArray linesArray;
        for (const QString& line : lines) {
            linesArray.append(line);
        }
        result["lines"] = linesArray;
        sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    else if (path == "/upload") {
        if (method == "GET") {
            sendHtml(socket, generateUploadPage());
        } else if (method == "POST") {
            handleUpload(socket, request);
        }
    }
    else {
        sendResponse(socket, 404, "text/plain", "Not Found");
    }
}

void ShotServer::sendResponse(QTcpSocket* socket, int statusCode, const QString& contentType,
                               const QByteArray& body, const QByteArray& extraHeaders)
{
    QString statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        default: statusText = "Unknown"; break;
    }

    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append(QString("Content-Type: %1\r\n").arg(contentType).toUtf8());
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Connection: close\r\n");
    if (!extraHeaders.isEmpty()) {
        response.append(extraHeaders);
    }
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->close();
}

void ShotServer::sendJson(QTcpSocket* socket, const QByteArray& json)
{
    sendResponse(socket, 200, "application/json", json);
}

void ShotServer::sendHtml(QTcpSocket* socket, const QString& html)
{
    sendResponse(socket, 200, "text/html; charset=utf-8", html.toUtf8());
}

void ShotServer::sendFile(QTcpSocket* socket, const QString& path, const QString& contentType)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 404, "text/plain", "File not found");
        return;
    }

    QByteArray data = file.readAll();
    QByteArray extraHeaders = QString("Content-Disposition: attachment; filename=\"shots.db\"\r\n").toUtf8();
    sendResponse(socket, 200, contentType, data, extraHeaders);
}

QString ShotServer::getLocalIpAddress() const
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            return address.toString();
        }
    }
    return "127.0.0.1";
}

QString ShotServer::generateIndexPage() const
{
    return generateShotListPage();
}

QString ShotServer::generateShotListPage() const
{
    QVariantList shots = m_storage->getShots(0, 100);

    QString rows;
    for (const QVariant& v : shots) {
        QVariantMap shot = v.toMap();

        int rating = qRound(shot["enjoyment"].toDouble() / 20.0);
        QString stars;
        for (int i = 0; i < 5; i++) {
            stars += (i < rating) ? "&#9733;" : "&#9734;";
        }

        double ratio = 0;
        if (shot["doseWeight"].toDouble() > 0) {
            ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
        }

        rows += QString(R"HTML(
            <div class="shot-card" onclick="toggleSelect(%1, this)" data-id="%1">
                <input type="checkbox" class="shot-checkbox" data-id="%1" onclick="event.stopPropagation(); toggleSelect(%1, this.parentElement)">
                <a href="/shot/%1" onclick="event.stopPropagation()" style="text-decoration:none;color:inherit;display:block;">
                    <div class="shot-header">
                        <span class="shot-profile">%2</span>
                        <span class="shot-date">%3</span>
                    </div>
                    <div class="shot-metrics">
                        <div class="shot-metric">
                            <span class="metric-value">%4g</span>
                            <span class="metric-label">in</span>
                        </div>
                        <div class="shot-arrow">&#8594;</div>
                        <div class="shot-metric">
                            <span class="metric-value">%5g</span>
                            <span class="metric-label">out</span>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">1:%6</span>
                            <span class="metric-label">ratio</span>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">%7s</span>
                            <span class="metric-label">time</span>
                        </div>
                    </div>
                    <div class="shot-footer">
                        <span class="shot-beans">%8 %9</span>
                        <span class="shot-rating">%10</span>
                    </div>
                </a>
            </div>
        )HTML")
        .arg(shot["id"].toLongLong())
        .arg(shot["profileName"].toString().toHtmlEscaped())
        .arg(shot["dateTime"].toString())
        .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
        .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)
        .arg(ratio, 0, 'f', 1)
        .arg(shot["duration"].toDouble(), 0, 'f', 1)
        .arg(shot["beanBrand"].toString().toHtmlEscaped())
        .arg(shot["beanType"].toString().toHtmlEscaped())
        .arg(stars);
    }

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shot History - Decenza DE1</title>
    <style>)HTML" R"HTML(
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --accent-dim: #a68a1f;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
            min-height: 100vh;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .logo {
            font-size: 1.25rem;
            font-weight: 600;
            color: var(--accent);
            text-decoration: none;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .shot-count {
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .shot-grid {
            display: grid;
            gap: 1rem;
            grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
        }
        .shot-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            text-decoration: none;
            color: inherit;
            transition: all 0.2s ease;
            display: block;
        }
        .shot-card:hover {
            background: var(--surface-hover);
            border-color: var(--accent);
            transform: translateY(-2px);
        }
        .shot-header {
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            margin-bottom: 0.75rem;
        }
        .shot-profile {
            font-weight: 600;
            font-size: 1rem;
            color: var(--text);
        }
        .shot-date {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .shot-metrics {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.75rem 0;
            border-top: 1px solid var(--border);
            border-bottom: 1px solid var(--border);
        }
        .shot-metric {
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .shot-metric .metric-value {
            font-size: 1.125rem;
            font-weight: 600;
            color: var(--accent);
        }
        .shot-metric .metric-label {
            font-size: 0.625rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .shot-arrow {
            color: var(--text-secondary);
            font-size: 1rem;
        }
        .shot-footer {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 0.75rem;
        }
        .shot-beans {
            font-size: 0.8125rem;
            color: var(--text-secondary);
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
            max-width: 60%%;
        }
        .shot-rating {
            color: var(--accent);
            font-size: 0.875rem;
        }
        .empty-state {
            text-align: center;
            padding: 4rem 2rem;
            color: var(--text-secondary);
        }
        .empty-state h2 {
            margin-bottom: 0.5rem;
            color: var(--text);
        }
        .search-bar {
            display: flex;
            gap: 1rem;
            margin-bottom: 1.5rem;
            flex-wrap: wrap;
            align-items: center;
        }
        .search-input {
            flex: 1;
            min-width: 200px;
            padding: 0.75rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--text);
            font-size: 1rem;
        }
        .search-input:focus {
            outline: none;
            border-color: var(--accent);
        }
        .search-input::placeholder { color: var(--text-secondary); }
        .compare-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            align-items: center;
            gap: 1rem;
            z-index: 100;
        }
        .compare-bar.visible { display: flex; }
        .compare-btn {
            padding: 0.75rem 2rem;
            background: var(--accent);
            color: var(--bg);
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .compare-btn:hover { opacity: 0.9; }
        .clear-btn {
            padding: 0.75rem 1.5rem;
            background: transparent;
            color: var(--text-secondary);
            border: 1px solid var(--border);
            border-radius: 8px;
            cursor: pointer;
        }
        .shot-card { position: relative; }
        .shot-checkbox {
            position: absolute;
            top: 1rem;
            right: 1rem;
            width: 24px;
            height: 24px;
            appearance: none;
            background: var(--bg);
            border: 2px solid var(--border);
            border-radius: 4px;
            cursor: pointer;
        }
        .shot-checkbox:checked {
            background: var(--accent);
            border-color: var(--accent);
        }
        .shot-checkbox:checked::after {
            content: "\\2713";
            color: var(--bg);
            font-size: 16px;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .shot-card.selected { border-color: var(--accent); }
        .header-right { display: flex; align-items: center; gap: 1rem; }
        .menu-wrapper { position: relative; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            border-bottom: 1px solid var(--border);
            white-space: nowrap;
        }
        .menu-item:last-child { border-bottom: none; }
        .menu-item:hover { background: var(--surface-hover); }
        .menu-item:first-child { border-radius: 7px 7px 0 0; }
        .menu-item:last-child { border-radius: 0 0 7px 7px; }
        .menu-item:only-child { border-radius: 7px; }
        @media (max-width: 600px) {
            .shot-grid { grid-template-columns: 1fr; }
            .container { padding: 1rem; padding-bottom: 5rem; }
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="logo">&#9749; Decenza DE1</a>
            <div class="header-right">
                <span class="shot-count">%1 shots</span>
                <div class="menu-wrapper">
                    <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                    <div class="menu-dropdown" id="menuDropdown">
                        <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                        <a href="/upload" class="menu-item">&#128230; Upload APK</a>
                        <a href="/database.db" class="menu-item">&#128190; Download Database</a>
                    </div>
                </div>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="search-bar">
            <input type="text" class="search-input" id="searchInput" placeholder="Search by profile, beans, notes..." oninput="filterShots()">
        </div>
        <div class="shot-grid" id="shotGrid">
            %2
        </div>
    </main>
    <div class="compare-bar" id="compareBar">
        <span id="selectedCount">0 selected</span>
        <button class="compare-btn" onclick="compareSelected()">Compare Shots</button>
        <button class="clear-btn" onclick="clearSelection()">Clear</button>
    </div>
    <script>
        var selectedShots = [];

        function toggleSelect(id, card) {
            var idx = selectedShots.indexOf(id);
            if (idx >= 0) {
                selectedShots.splice(idx, 1);
                card.classList.remove("selected");
            } else {
                if (selectedShots.length < 5) {
                    selectedShots.push(id);
                    card.classList.add("selected");
                }
            }
            updateCompareBar();
        }

        function updateCompareBar() {
            var bar = document.getElementById("compareBar");
            var count = document.getElementById("selectedCount");
            if (selectedShots.length >= 2) {
                bar.classList.add("visible");
                count.textContent = selectedShots.length + " selected";
            } else {
                bar.classList.remove("visible");
            }
            // Update checkboxes
            document.querySelectorAll(".shot-checkbox").forEach(function(cb) {
                cb.checked = selectedShots.indexOf(parseInt(cb.dataset.id)) >= 0;
            });
        }

        function clearSelection() {
            selectedShots = [];
            document.querySelectorAll(".shot-card").forEach(function(c) { c.classList.remove("selected"); });
            updateCompareBar();
        }

        function compareSelected() {
            if (selectedShots.length >= 2) {
                window.location.href = "/compare/" + selectedShots.join(",");
            }
        }

        function filterShots() {
            var query = document.getElementById("searchInput").value.toLowerCase();
            document.querySelectorAll(".shot-card").forEach(function(card) {
                var text = card.textContent.toLowerCase();
                card.style.display = text.includes(query) ? "" : "none";
            });
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        // Close menu when clicking outside
        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });
    </script>
</body>
</html>
)HTML").arg(m_storage->totalShots())
   .arg(rows.isEmpty() ? "<div class='empty-state'><h2>No shots yet</h2><p>Pull some espresso to see your history here</p></div>" : rows);
}

QString ShotServer::generateShotDetailPage(qint64 shotId) const
{
    QVariantMap shot = m_storage->getShot(shotId);
    if (shot.isEmpty()) {
        return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>"
                  "<body style=\"background:#0d1117;color:#fff;font-family:sans-serif;padding:2rem;\">"
                  "<h1>Shot not found</h1><a href=\"/\" style=\"color:#c9a227;\">Back to list</a></body></html>");
    }

    double ratio = 0;
    if (shot["doseWeight"].toDouble() > 0) {
        ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
    }

    int rating = qRound(shot["enjoyment"].toDouble() / 20.0);
    QString stars;
    for (int i = 0; i < 5; i++) {
        stars += (i < rating) ? "&#9733;" : "&#9734;";
    }

    // Convert time-series data to JSON arrays for Chart.js
    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    QString pressureData = pointsToJson(shot["pressure"].toList());
    QString flowData = pointsToJson(shot["flow"].toList());
    QString tempData = pointsToJson(shot["temperature"].toList());
    QString weightData = pointsToJson(shot["weight"].toList());
    QString pressureGoalData = pointsToJson(shot["pressureGoal"].toList());
    QString flowGoalData = pointsToJson(shot["flowGoal"].toList());

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%1 - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>)HTML" R"HTML(
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
            line-height: 1;
            padding: 0.25rem;
        }
        .back-btn:hover { color: var(--accent); }
        .header-title {
            flex: 1;
        }
        .header-title h1 {
            font-size: 1.125rem;
            font-weight: 600;
        }
        .header-title .subtitle {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .metrics-bar {
            display: flex;
            gap: 1rem;
            flex-wrap: wrap;
            margin-bottom: 1.5rem;
        }
        .metric-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            min-width: 100px;
            text-align: center;
        }
        .metric-card .value {
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--accent);
        }
        .metric-card .label {
            font-size: 0.6875rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.5rem;
        }
        .chart-title {
            font-size: 1rem;
            font-weight: 600;
        }
        .chart-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.375rem 0.75rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.75rem;
            cursor: pointer;
            transition: all 0.15s ease;
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface-hover); color: var(--text); }
        .toggle-btn .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .chart-wrapper {
            position: relative;
            height: 400px;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 1rem;
        }
        .info-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .info-card h3 {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .info-row:last-child { border-bottom: none; }
        .info-row .label { color: var(--text-secondary); }
        .info-row .value { font-weight: 500; }
        .notes-text {
            color: var(--text-secondary);
            font-style: italic;
        }
        .rating { color: var(--accent); font-size: 1.125rem; }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface-hover); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 300px; }
            .metrics-bar { justify-content: center; }
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <div class="header-title">
                <h1>%1</h1>
                <div class="subtitle">%2</div>
            </div>
            <div class="menu-wrapper">
                <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                <div class="menu-dropdown" id="menuDropdown">
                    <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                    <a href="/upload" class="menu-item">&#128230; Upload APK</a>
                    <a href="/database.db" class="menu-item">&#128190; Download Database</a>
                </div>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="metrics-bar">
            <div class="metric-card">
                <div class="value">%3g</div>
                <div class="label">Dose</div>
            </div>
            <div class="metric-card">
                <div class="value">%4g</div>
                <div class="label">Yield</div>
            </div>
            <div class="metric-card">
                <div class="value">1:%5</div>
                <div class="label">Ratio</div>
            </div>
            <div class="metric-card">
                <div class="value">%6s</div>
                <div class="label">Time</div>
            </div>
            <div class="metric-card">
                <div class="value rating">%7</div>
                <div class="label">Rating</div>
            </div>
        </div>

        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="chart-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleDataset(0, this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleDataset(1, this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleDataset(2, this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleDataset(3, this)">
                        <span class="dot"></span> Temp
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="shotChart"></canvas>
            </div>
        </div>

        <div class="info-grid">
            <div class="info-card">
                <h3>Beans</h3>
                <div class="info-row">
                    <span class="label">Brand</span>
                    <span class="value">%8</span>
                </div>
                <div class="info-row">
                    <span class="label">Type</span>
                    <span class="value">%9</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Date</span>
                    <span class="value">%10</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Level</span>
                    <span class="value">%11</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Grinder</h3>
                <div class="info-row">
                    <span class="label">Model</span>
                    <span class="value">%12</span>
                </div>
                <div class="info-row">
                    <span class="label">Setting</span>
                    <span class="value">%13</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Notes</h3>
                <p class="notes-text">%14</p>
            </div>
        </div>
    </main>

    <script>
        const pressureData = %15;
        const flowData = %16;
        const weightData = %17;
        const tempData = %18;
        const pressureGoalData = %19;
        const flowGoalData = %20;

        // Custom tooltip positioner - follows mouse
        Chart.Tooltip.positioners.cursor = function(elements, eventPosition) {
            return { x: eventPosition.x, y: eventPosition.y };
        };

        const ctx = document.getElementById('shotChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            data: {
                datasets: [
                    {
                        label: 'Pressure',
                        data: pressureData,
                        borderColor: '#18c37e',
                        backgroundColor: 'rgba(24, 195, 126, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Flow',
                        data: flowData,
                        borderColor: '#4e85f4',
                        backgroundColor: 'rgba(78, 133, 244, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Yield',
                        data: weightData,
                        borderColor: '#a2693d',
                        backgroundColor: 'rgba(162, 105, 61, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y2'
                    },
                    {
                        label: 'Temp',
                        data: tempData,
                        borderColor: '#e73249',
                        backgroundColor: 'rgba(231, 50, 73, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y3'
                    },
                    {
                        label: 'Pressure Goal',
                        data: pressureGoalData,
                        borderColor: '#69fdb3',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Flow Goal',
                        data: flowGoalData,
                        borderColor: '#7aaaff',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y'
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'nearest',
                    axis: 'x',
                    intersect: false
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: '#161b22',
                        borderColor: '#30363d',
                        borderWidth: 1,
                        titleColor: '#e6edf3',
                        bodyColor: '#8b949e',
                        padding: 12,
                        displayColors: true,
                        position: 'cursor',
                        callbacks: {
                            title: function(items) {
                                return items[0].parsed.x.toFixed(1) + "s";
                            },
                            label: function(context) {
                                var label = context.dataset.label || "";
                                var value = context.parsed.y.toFixed(1);
                                var unit = "";
                                if (label.includes("Pressure")) unit = " bar";
                                else if (label.includes("Flow")) unit = " ml/s";
                                else if (label.includes("Yield")) unit = " g";
                                else if (label.includes("Temp")) unit = " C";
                                return label + ": " + value + unit;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        type: 'linear',
                        title: { display: true, text: 'Time (s)', color: '#8b949e' },
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },
                    y: {
                        type: 'linear',
                        position: 'left',
                        title: { display: true, text: 'Pressure / Flow', color: '#8b949e' },
                        min: 0,
                        max: 12,
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },
                    y2: {
                        type: 'linear',
                        position: 'right',
                        title: { display: true, text: 'Yield (g)', color: '#a2693d' },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: '#a2693d' }
                    },
                    y3: {
                        type: 'linear',
                        position: 'right',
                        title: { display: false },
                        min: 80,
                        max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleDataset(index, btn) {
            const meta = chart.getDatasetMeta(index);
            meta.hidden = !meta.hidden;
            btn.classList.toggle('active');

            // Also toggle goal lines for pressure/flow
            if (index === 0) chart.getDatasetMeta(4).hidden = meta.hidden;
            if (index === 1) chart.getDatasetMeta(5).hidden = meta.hidden;

            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });
    </script>
</body>
</html>
)HTML")
    .arg(shot["profileName"].toString().toHtmlEscaped())
    .arg(shot["dateTime"].toString())
    .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
    .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)
    .arg(ratio, 0, 'f', 1)
    .arg(shot["duration"].toDouble(), 0, 'f', 1)
    .arg(stars)
    .arg(shot["beanBrand"].toString().isEmpty() ? "-" : shot["beanBrand"].toString().toHtmlEscaped())
    .arg(shot["beanType"].toString().isEmpty() ? "-" : shot["beanType"].toString().toHtmlEscaped())
    .arg(shot["roastDate"].toString().isEmpty() ? "-" : shot["roastDate"].toString())
    .arg(shot["roastLevel"].toString().isEmpty() ? "-" : shot["roastLevel"].toString().toHtmlEscaped())
    .arg(shot["grinderModel"].toString().isEmpty() ? "-" : shot["grinderModel"].toString().toHtmlEscaped())
    .arg(shot["grinderSetting"].toString().isEmpty() ? "-" : shot["grinderSetting"].toString().toHtmlEscaped())
    .arg(shot["espressoNotes"].toString().isEmpty() ? "No notes" : shot["espressoNotes"].toString().toHtmlEscaped())
    .arg(pressureData)
    .arg(flowData)
    .arg(weightData)
    .arg(tempData)
    .arg(pressureGoalData)
    .arg(flowGoalData);
}

QString ShotServer::generateComparisonPage(const QList<qint64>& shotIds) const
{
    // Load all shots
    QList<QVariantMap> shots;
    for (qint64 id : shotIds) {
        QVariantMap shot = m_storage->getShot(id);
        if (!shot.isEmpty()) {
            shots << shot;
        }
    }

    if (shots.size() < 2) {
        return QStringLiteral("<!DOCTYPE html><html><body>Not enough valid shots to compare</body></html>");
    }

    // Colors for each shot (up to 5)
    QStringList shotColors = {"#c9a227", "#e85d75", "#4ecdc4", "#a855f7", "#f97316"};

    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Build datasets for each shot
    QString datasets;
    QString legendItems;
    int shotIndex = 0;

    for (const QVariantMap& shot : shots) {
        QString color = shotColors[shotIndex % shotColors.size()];
        QString name = shot["profileName"].toString();
        QString date = shot["dateTime"].toString().left(10);
        QString label = QString("%1 (%2)").arg(name, date);

        QString pressureData = pointsToJson(shot["pressure"].toList());
        QString flowData = pointsToJson(shot["flow"].toList());
        QString weightData = pointsToJson(shot["weight"].toList());
        QString tempData = pointsToJson(shot["temperature"].toList());

        // Add datasets for this shot
        datasets += QString(R"HTML(
            { label: "Pressure - %1", data: %2, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", shotIndex: %4, curveType: "pressure" },
            { label: "Flow - %1", data: %5, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", borderDash: [5,3], shotIndex: %4, curveType: "flow" },
            { label: "Yield - %1", data: %6, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y2", borderDash: [2,2], shotIndex: %4, curveType: "weight" },
            { label: "Temp - %1", data: %7, borderColor: "%3", borderWidth: 1, pointRadius: 0, tension: 0.3, yAxisID: "y3", borderDash: [8,4], shotIndex: %4, curveType: "temp" },
        )HTML").arg(label.toHtmlEscaped(), pressureData, color).arg(shotIndex).arg(flowData, weightData, tempData);

        double ratio = shot["doseWeight"].toDouble() > 0 ?
            shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble() : 0;

        legendItems += QString(R"HTML(
            <div class="legend-item">
                <span class="legend-color" style="background:%1"></span>
                <div class="legend-info">
                    <div class="legend-name">%2</div>
                    <div class="legend-details">%3 | %4g in | %5g out | 1:%6 | %7s</div>
                </div>
            </div>
        )HTML").arg(color)
               .arg(label.toHtmlEscaped())
               .arg(date)
               .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
               .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)
               .arg(ratio, 0, 'f', 1)
               .arg(shot["duration"].toDouble(), 0, 'f', 1);

        shotIndex++;
    }

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Compare Shots - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.75rem;
        }
        .chart-title { font-size: 1rem; font-weight: 600; }
        .curve-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.8125rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface); color: var(--text); border-color: var(--text); }
        .toggle-btn .dot { width: 10px; height: 10px; border-radius: 50%; }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .chart-wrapper { position: relative; height: 450px; }
        .legend {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
        }
        .legend-title {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .legend-item:last-child { border-bottom: none; }
        .legend-color {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            flex-shrink: 0;
        }
        .legend-name { font-weight: 500; }
        .legend-details { font-size: 0.75rem; color: var(--text-secondary); }
        .curve-legend {
            display: flex;
            gap: 1.5rem;
            margin-top: 1rem;
            padding-top: 1rem;
            border-top: 1px solid var(--border);
            flex-wrap: wrap;
        }
        .curve-legend-item {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .curve-line {
            width: 24px;
            height: 2px;
        }
        .curve-line.solid { background: var(--text-secondary); }
        .curve-line.dashed { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 4px, transparent 4px, transparent 7px); }
        .curve-line.dotted { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 2px, transparent 2px, transparent 5px); }
        .curve-line.longdash { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 8px, transparent 8px, transparent 12px); }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 350px; }
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Compare %1 Shots</h1>
            <div class="menu-wrapper">
                <button class="menu-btn" onclick="toggleMenu()" aria-label="Menu">&#9776;</button>
                <div class="menu-dropdown" id="menuDropdown">
                    <a href="/debug" class="menu-item">&#128196; Live Debug Log</a>
                    <a href="/upload" class="menu-item">&#128230; Upload APK</a>
                    <a href="/database.db" class="menu-item">&#128190; Download Database</a>
                </div>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="curve-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleCurve('pressure', this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleCurve('flow', this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleCurve('weight', this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleCurve('temp', this)">
                        <span class="dot"></span> Temp
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="compareChart"></canvas>
            </div>
        </div>
        <div class="legend">
            <div class="legend-title">Shots</div>
            %2
            <div class="curve-legend">
                <div class="curve-legend-item"><span class="curve-line solid"></span> Pressure</div>
                <div class="curve-legend-item"><span class="curve-line dashed"></span> Flow</div>
                <div class="curve-legend-item"><span class="curve-line dotted"></span> Yield</div>
                <div class="curve-legend-item"><span class="curve-line longdash"></span> Temp</div>
            </div>
        </div>
    </main>
    <script>
        Chart.Tooltip.positioners.cursor = function(elements, eventPosition) {
            return { x: eventPosition.x, y: eventPosition.y };
        };

        var visibleCurves = { pressure: true, flow: true, weight: true, temp: true };

        var ctx = document.getElementById("compareChart").getContext("2d");
        var chart = new Chart(ctx, {
            type: "line",
            data: {
                datasets: [
                    %3
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: { mode: "nearest", axis: "x", intersect: false },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: "#161b22",
                        borderColor: "#30363d",
                        borderWidth: 1,
                        titleColor: "#e6edf3",
                        bodyColor: "#8b949e",
                        padding: 12,
                        position: "cursor",
                        callbacks: {
                            title: function(items) { return items[0].parsed.x.toFixed(1) + "s"; }
                        }
                    }
                },
                scales: {
                    x: {
                        type: "linear",
                        title: { display: true, text: "Time (s)", color: "#8b949e" },
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y: {
                        type: "linear",
                        position: "left",
                        title: { display: true, text: "Pressure / Flow", color: "#8b949e" },
                        min: 0, max: 12,
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y2: {
                        type: "linear",
                        position: "right",
                        title: { display: true, text: "Yield (g)", color: "#a2693d" },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: "#a2693d" }
                    },
                    y3: {
                        type: "linear",
                        position: "right",
                        title: { display: false },
                        min: 80, max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleCurve(curveType, btn) {
            visibleCurves[curveType] = !visibleCurves[curveType];
            btn.classList.toggle("active");

            chart.data.datasets.forEach(function(ds, i) {
                if (ds.curveType === curveType) {
                    chart.getDatasetMeta(i).hidden = !visibleCurves[curveType];
                }
            });
            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });
    </script>
</body>
</html>
)HTML").arg(shots.size()).arg(legendItems).arg(datasets);
}

QString ShotServer::generateDebugPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Debug Log - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; flex: 1; }
        .status {
            font-size: 0.75rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #18c37e;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .controls {
            display: flex;
            gap: 0.5rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
        }
        .btn:hover { border-color: var(--accent); color: var(--accent); }
        .btn.active { background: var(--accent); color: var(--bg); border-color: var(--accent); }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1rem;
        }
        .log-container {
            background: #000;
            border: 1px solid var(--border);
            border-radius: 8px;
            height: calc(100vh - 120px);
            overflow-y: auto;
            font-family: "Consolas", "Monaco", "Courier New", monospace;
            font-size: 12px;
            padding: 0.5rem;
        }
        .log-line {
            white-space: pre;
            padding: 1px 0;
        }
        .log-line:hover { background: rgba(255,255,255,0.05); }
        .DEBUG { color: #8b949e; }
        .INFO { color: #58a6ff; }
        .WARN { color: #d29922; }
        .ERROR { color: #f85149; }
        .FATAL { color: #ff0000; font-weight: bold; }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Live Debug Log</h1>
            <div class="status">
                <span class="status-dot"></span>
                <span id="lineCount">0 lines</span>
            </div>
            <div class="controls">
                <button class="btn active" id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-scroll</button>
                <button class="btn" onclick="clearLog()">Clear</button>
            </div>
        </div>
    </header>
    <main class="container">
        <div class="log-container" id="logContainer"></div>
    </main>
    <script>
        var lastIndex = 0;
        var autoScroll = true;
        var container = document.getElementById("logContainer");
        var lineCountEl = document.getElementById("lineCount");

        function colorize(line) {
            var category = "";
            if (line.includes("] DEBUG ")) category = "DEBUG";
            else if (line.includes("] INFO ")) category = "INFO";
            else if (line.includes("] WARN ")) category = "WARN";
            else if (line.includes("] ERROR ")) category = "ERROR";
            else if (line.includes("] FATAL ")) category = "FATAL";
            return "<div class=\"log-line " + category + "\">" + escapeHtml(line) + "</div>";
        }

        function escapeHtml(text) {
            var div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        function fetchLogs() {
            fetch("/api/debug?after=" + lastIndex)
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.lines && data.lines.length > 0) {
                        var html = "";
                        for (var i = 0; i < data.lines.length; i++) {
                            html += colorize(data.lines[i]);
                        }
                        container.insertAdjacentHTML("beforeend", html);
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    }
                    lastIndex = data.lastIndex;
                    lineCountEl.textContent = lastIndex + " lines";
                });
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            document.getElementById("autoScrollBtn").classList.toggle("active", autoScroll);
            if (autoScroll) {
                container.scrollTop = container.scrollHeight;
            }
        }

        function clearLog() {
            container.innerHTML = "";
            lastIndex = 0;
        }

        // Poll every 500ms
        setInterval(fetchLogs, 500);
        fetchLogs();
    </script>
</body>
</html>
)HTML");
}

QString ShotServer::generateUploadPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Upload APK - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #f85149;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
        }
        .header-content {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 2rem 1.5rem;
        }
        .upload-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2rem;
        }
        .upload-zone {
            border: 2px dashed var(--border);
            border-radius: 8px;
            padding: 3rem 2rem;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
        }
        .upload-zone:hover, .upload-zone.dragover {
            border-color: var(--accent);
            background: rgba(201, 162, 39, 0.05);
        }
        .upload-zone.uploading {
            border-color: var(--text-secondary);
            cursor: default;
        }
        .upload-icon {
            font-size: 3rem;
            margin-bottom: 1rem;
        }
        .upload-text {
            color: var(--text-secondary);
            margin-bottom: 0.5rem;
        }
        .upload-hint {
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        input[type="file"] { display: none; }
        .progress-bar {
            display: none;
            height: 8px;
            background: var(--border);
            border-radius: 4px;
            margin-top: 1.5rem;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: var(--accent);
            width: 0%;
            transition: width 0.3s;
        }
        .status-message {
            margin-top: 1rem;
            padding: 1rem;
            border-radius: 8px;
            display: none;
        }
        .status-message.success {
            display: block;
            background: rgba(24, 195, 126, 0.1);
            border: 1px solid var(--success);
            color: var(--success);
        }
        .status-message.error {
            display: block;
            background: rgba(248, 81, 73, 0.1);
            border: 1px solid var(--error);
            color: var(--error);
        }
        .file-info {
            margin-top: 1rem;
            padding: 1rem;
            background: var(--bg);
            border-radius: 8px;
            display: none;
        }
        .file-name {
            font-weight: 600;
            margin-bottom: 0.25rem;
        }
        .file-size {
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        .warning {
            margin-top: 1.5rem;
            padding: 1rem;
            background: rgba(210, 153, 34, 0.1);
            border: 1px solid #d29922;
            border-radius: 8px;
            color: #d29922;
            font-size: 0.875rem;
        }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Upload APK</h1>
        </div>
    </header>
    <main class="container">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#128230;</div>
                <div class="upload-text">Click or drag APK file here</div>
                <div class="upload-hint">Decenza_DE1_*.apk</div>
            </div>
            <input type="file" id="fileInput" accept=".apk" onchange="handleFile(this.files[0])">
            <div class="file-info" id="fileInfo">
                <div class="file-name" id="fileName"></div>
                <div class="file-size" id="fileSize"></div>
            </div>
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
            <div class="warning">
                &#9888; After upload completes, Android will prompt to install the APK.
                The current app will close during installation.
            </div>
        </div>
    </main>
    <script>
        var uploadZone = document.getElementById("uploadZone");
        var fileInfo = document.getElementById("fileInfo");
        var progressBar = document.getElementById("progressBar");
        var progressFill = document.getElementById("progressFill");
        var statusMessage = document.getElementById("statusMessage");

        uploadZone.addEventListener("dragover", function(e) {
            e.preventDefault();
            uploadZone.classList.add("dragover");
        });
        uploadZone.addEventListener("dragleave", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
        });
        uploadZone.addEventListener("drop", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
            if (e.dataTransfer.files.length > 0) {
                handleFile(e.dataTransfer.files[0]);
            }
        });

        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        function handleFile(file) {
            if (!file) return;
            if (!file.name.endsWith(".apk")) {
                showStatus("error", "Please select an APK file");
                return;
            }

            document.getElementById("fileName").textContent = file.name;
            document.getElementById("fileSize").textContent = formatSize(file.size);
            fileInfo.style.display = "block";

            uploadFile(file);
        }

        function uploadFile(file) {
            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";
            statusMessage.className = "status-message";
            statusMessage.style.display = "none";

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/upload", true);

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                if (xhr.status === 200) {
                    showStatus("success", "Upload complete! Installing...");
                } else {
                    showStatus("error", "Upload failed: " + xhr.responseText);
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Network error during upload");
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", file.name);
            xhr.send(file);
        }

        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }
    </script>
</body>
</html>
)HTML");
}

void ShotServer::handleUpload(QTcpSocket* socket, const QByteArray& request)
{
    // Parse headers to get filename and content
    int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        sendResponse(socket, 400, "text/plain", "Invalid request");
        return;
    }

    QString headers = QString::fromUtf8(request.left(headerEnd));
    QByteArray body = request.mid(headerEnd + 4);

    // Get filename from X-Filename header
    QString filename = "uploaded.apk";
    for (const QString& line : headers.split("\r\n")) {
        if (line.startsWith("X-Filename:", Qt::CaseInsensitive)) {
            filename = line.mid(11).trimmed();
            break;
        }
    }

    if (!filename.endsWith(".apk", Qt::CaseInsensitive)) {
        sendResponse(socket, 400, "text/plain", "Only APK files are allowed");
        return;
    }

    // Save to cache/downloads directory
    QString savePath;
#ifdef Q_OS_ANDROID
    savePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#endif

    QDir().mkpath(savePath);
    QString fullPath = savePath + "/" + filename;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        sendResponse(socket, 500, "text/plain", "Failed to save file: " + file.errorString().toUtf8());
        return;
    }

    file.write(body);
    file.close();

    qDebug() << "APK uploaded:" << fullPath << "size:" << body.size();

    // Trigger installation on Android
    installApk(fullPath);

    sendResponse(socket, 200, "text/plain", "Upload complete: " + fullPath.toUtf8());
}

void ShotServer::installApk(const QString& apkPath)
{
#ifdef Q_OS_ANDROID
    qDebug() << "Installing APK:" << apkPath;

    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "Failed to get Android activity";
        return;
    }

    QJniObject context = activity.callObjectMethod(
        "getApplicationContext",
        "()Landroid/content/Context;");

    // Create file URI using FileProvider for Android 7+
    QJniObject javaPath = QJniObject::fromString(apkPath);
    QJniObject file = QJniObject("java/io/File", "(Ljava/lang/String;)V",
                                  javaPath.object<jstring>());

    // Get package name for FileProvider authority
    QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;");
    QString authority = packageName.toString() + ".fileprovider";

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(),
        QJniObject::fromString(authority).object<jstring>(),
        file.object());

    if (!uri.isValid()) {
        qWarning() << "Failed to create content URI for APK";
        return;
    }

    // Create install intent
    QJniObject intent("android/content/Intent");
    QJniObject actionView = QJniObject::fromString("android.intent.action.VIEW");
    intent.callObjectMethod("setAction",
                            "(Ljava/lang/String;)Landroid/content/Intent;",
                            actionView.object<jstring>());

    QJniObject mimeType = QJniObject::fromString("application/vnd.android.package-archive");
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(),
                            mimeType.object<jstring>());

    // Add flags
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x00000001); // FLAG_GRANT_READ_URI_PERMISSION
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

    // Start activity
    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());

    qDebug() << "APK install intent launched";
#else
    qDebug() << "APK installation only supported on Android. File saved to:" << apkPath;
#endif
}
