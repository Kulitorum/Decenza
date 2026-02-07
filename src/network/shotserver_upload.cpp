#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
#include "version.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#ifndef Q_OS_IOS
#include <QProcess>
#endif
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

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
)HTML" R"HTML(
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
    qsizetype headerEndPos = request.indexOf("\r\n\r\n");
    if (headerEndPos < 0) {
        sendResponse(socket, 400, "text/plain", "Invalid request");
        return;
    }

    QString headers = QString::fromUtf8(request.left(headerEndPos));
    QByteArray body = request.mid(headerEndPos + 4);

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

// ============================================================================
// Personal Media Upload
// ============================================================================

QString ShotServer::generateMediaUploadPage() const
{
    // Get current personal media list for display
    QString mediaListHtml;
    if (m_screensaverManager) {
        QVariantList media = m_screensaverManager->getPersonalMediaList();
        if (!media.isEmpty()) {
            mediaListHtml = R"HTML(
            <div class="media-list">
                <h3>Current Personal Media</h3>
                <div class="media-grid">)HTML";

            for (const QVariant& v : std::as_const(media)) {
                QVariantMap m = v.toMap();
                QString type = m["type"].toString();
                QString filename = m["filename"].toString();
                qint64 bytes = m["bytes"].toLongLong();
                int id = m["id"].toInt();
                QString sizeStr = bytes < 1024*1024
                    ? QString::number(bytes/1024) + " KB"
                    : QString::number(bytes/(1024*1024)) + " MB";

                mediaListHtml += QString(R"HTML(
                    <div class="media-item" data-id="%1">
                        <div class="media-icon">%2</div>
                        <div class="media-info">
                            <div class="media-name">%3</div>
                            <div class="media-size">%4</div>
                        </div>
                        <button class="delete-btn" onclick="deleteMedia(%1)">&#128465;</button>
                    </div>)HTML")
                    .arg(id)
                    .arg(type == "video" ? "&#127909;" : "&#128247;")
                    .arg(filename.toHtmlEscaped())
                    .arg(sizeStr);
            }

            mediaListHtml += QString(R"HTML(
                </div>
                <button class="delete-all-btn" onclick="deleteAllMedia(%1)">Delete All (%1 items)</button>
            </div>)HTML").arg(media.size());
        }
    }

    // Build HTML in chunks to avoid MSVC string literal size limit
    QString html;

    // Part 1: Head and CSS variables
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Upload Screensaver Media - Decenza DE1</title>
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
)HTML";

    // Part 2: More CSS
    html += R"HTML(
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
            margin-bottom: 1.5rem;
        }
)HTML";

    // Part 3: Upload zone CSS
    html += R"HTML(
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
        .upload-icon { font-size: 3rem; margin-bottom: 1rem; }
        .upload-text { color: var(--text-secondary); margin-bottom: 0.5rem; }
        .upload-hint { color: var(--text-secondary); font-size: 0.875rem; }
        input[type="file"] { display: none; }
)HTML";

    // Part 4: Progress and status CSS
    html += R"HTML(
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
        .status-message.processing {
            display: block;
            background: rgba(201, 162, 39, 0.1);
            border: 1px solid var(--accent);
            color: var(--accent);
        }
)HTML";

    // Part 5: File info and media list CSS
    html += R"HTML(
        .file-info {
            margin-top: 1rem;
            padding: 1rem;
            background: var(--bg);
            border-radius: 8px;
            display: none;
        }
        .file-name { font-weight: 600; margin-bottom: 0.25rem; }
        .file-size { color: var(--text-secondary); font-size: 0.875rem; }
        .info-box {
            margin-top: 1.5rem;
            padding: 1rem;
            background: rgba(201, 162, 39, 0.1);
            border: 1px solid var(--accent);
            border-radius: 8px;
            font-size: 0.875rem;
        }
        .info-box h4 { margin-bottom: 0.5rem; color: var(--accent); }
        .info-box ul { margin-left: 1.25rem; color: var(--text-secondary); }
        .media-list {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
        }
        .media-list h3 { margin-bottom: 1rem; font-size: 1rem; }
        .media-grid { display: flex; flex-direction: column; gap: 0.75rem; }
)HTML";

    // Part 6: Media item CSS
    html += R"HTML(
        .media-item {
            display: flex;
            align-items: center;
            gap: 1rem;
            padding: 0.75rem;
            background: var(--bg);
            border-radius: 8px;
        }
        .media-icon { font-size: 1.5rem; }
        .media-info { flex: 1; min-width: 0; }
        .media-name { font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .media-size { color: var(--text-secondary); font-size: 0.75rem; }
        .delete-btn {
            background: transparent;
            border: none;
            color: var(--text-secondary);
            font-size: 1.25rem;
            cursor: pointer;
            padding: 0.5rem;
            border-radius: 4px;
        }
        .delete-btn:hover { background: rgba(248, 81, 73, 0.2); color: var(--error); }
        .delete-all-btn {
            margin-top: 1rem;
            padding: 0.75rem 1.5rem;
            background: var(--error);
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-size: 0.9rem;
        }
        .delete-all-btn:hover { background: #c93c37; }
    </style>
</head>
)HTML";

    // Part 7: Body and upload form
    html += R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Upload Screensaver Media</h1>
        </div>
    </header>
    <main class="container">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#127912;</div>
                <div class="upload-text">Click or drag media files here</div>
                <div class="upload-hint">JPG, PNG, GIF, WebP, MP4, WebM</div>
            </div>
            <input type="file" id="fileInput" accept=".jpg,.jpeg,.png,.gif,.webp,.mp4,.webm,.mov" multiple onchange="handleFiles(this.files)">
            <div class="file-info" id="fileInfo">
                <div class="file-name" id="fileName"></div>
                <div class="file-size" id="fileSize"></div>
            </div>
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
            <div class="info-box">
                <h4>Processing</h4>
                <ul>
                    <li><b>Images</b> - Resized in browser (no tools needed)</li>
                    <li><b>Videos</b> - Resized on server (requires FFmpeg)</li>
                    <li><b>Photo dates</b> - Best results with exiftool</li>
                </ul>
                <details style="margin-top:0.75rem">
                    <summary style="cursor:pointer;color:var(--accent)">Windows install commands (for videos)</summary>
                    <pre style="background:var(--bg);padding:0.5rem;margin-top:0.5rem;border-radius:4px;font-size:0.75rem;overflow-x:auto">winget install Gyan.FFmpeg
winget install OliverBetz.ExifTool</pre>
                </details>
            </div>
        </div>
)HTML";

    // Insert media list HTML
    html += mediaListHtml;

    // Part 8: Script - event listeners
    html += R"HTML(
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
                handleFiles(e.dataTransfer.files);
            }
        });
)HTML";

    // Part 9: Script - utility functions
    html += R"HTML(
        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        var uploadQueue = [];
        var isUploading = false;
        var totalFiles = 0;
        var completedFiles = 0;
        var skippedFiles = [];
        var failedFiles = [];

        function handleFiles(files) {
            // Reset counters for new batch
            if (!isUploading) {
                totalFiles = 0;
                completedFiles = 0;
                skippedFiles = [];
                failedFiles = [];
            }
            for (var i = 0; i < files.length; i++) {
                var file = files[i];
                var ext = file.name.split('.').pop().toLowerCase();
                var validExts = ['jpg','jpeg','png','gif','webp','mp4','webm','mov'];
                if (validExts.indexOf(ext) === -1) {
                    showStatus("error", "Unsupported file type: " + file.name);
                    continue;
                }
                uploadQueue.push(file);
                totalFiles++;
            }
            processQueue();
        }

        function processQueue() {
            if (isUploading || uploadQueue.length === 0) {
                // All done - check if we should reload
                var processed = completedFiles + skippedFiles.length + failedFiles.length;
                if (!isUploading && totalFiles > 0 && processed === totalFiles) {
                    var msg = "Uploaded: " + completedFiles;
                    if (skippedFiles.length > 0) msg += ", Skipped: " + skippedFiles.length;
                    if (failedFiles.length > 0) {
                        msg += ", Failed: " + failedFiles.length;
                        // Show details for first few failed files
                        var details = failedFiles.slice(0, 3).map(function(f) {
                            return f.name + " (" + f.error + ")";
                        }).join("; ");
                        if (failedFiles.length > 3) details += "...";
                        msg += " - " + details;
                    }
                    if (failedFiles.length > 0) {
                        showStatus("error", msg);
                        // Still reload after delay to show successfully uploaded files
                        if (completedFiles > 0) {
                            setTimeout(function() { location.reload(); }, 5000);
                        }
                    } else {
                        showStatus("success", msg);
                        if (completedFiles > 0) {
                            setTimeout(function() { location.reload(); }, 1500);
                        }
                    }
                }
                return;
            }
            isUploading = true;
            var file = uploadQueue.shift();
            uploadFile(file);
        }
)HTML";

    // Part 10: Script - upload function
    html += R"HTML(
        function resizeImageInBrowser(file, maxWidth, maxHeight, callback) {
            var img = new Image();
            img.onload = function() {
                // Create canvas at exact target size
                var canvas = document.createElement("canvas");
                canvas.width = maxWidth;
                canvas.height = maxHeight;
                var ctx = canvas.getContext("2d");

                // Fill with black background
                ctx.fillStyle = "black";
                ctx.fillRect(0, 0, maxWidth, maxHeight);

                // Scale image to fit within bounds (letterbox/pillarbox)
                var scale = Math.min(maxWidth / img.width, maxHeight / img.height, 1);
                var scaledWidth = Math.round(img.width * scale);
                var scaledHeight = Math.round(img.height * scale);

                // Center the image on the canvas
                var x = Math.round((maxWidth - scaledWidth) / 2);
                var y = Math.round((maxHeight - scaledHeight) / 2);

                ctx.drawImage(img, x, y, scaledWidth, scaledHeight);
                canvas.toBlob(function(blob) {
                    callback(blob);
                }, "image/jpeg", 0.85);
            };
            img.onerror = function() { callback(null); };
            img.src = URL.createObjectURL(file);
        }

        function uploadFile(file) {
            var currentNum = completedFiles + failedFiles.length + 1;
            var statusText = totalFiles > 1 ? " (" + currentNum + "/" + totalFiles + ")" : "";

            document.getElementById("fileName").textContent = file.name + statusText;
            document.getElementById("fileSize").textContent = formatSize(file.size);
            fileInfo.style.display = "block";

            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";

            var ext = file.name.split(".").pop().toLowerCase();
            var isStandardImage = ["jpg","jpeg","png","gif","webp"].indexOf(ext) >= 0;

            if (isStandardImage) {
                showStatus("processing", "Resizing" + statusText + "...");
                resizeImageInBrowser(file, 1280, 800, function(resizedBlob) {
                    if (resizedBlob) {
                        showStatus("processing", "Uploading" + statusText + " (" + formatSize(resizedBlob.size) + ")...");
                        doUpload(file.name.replace(/\.[^.]+$/, ".jpg"), resizedBlob);
                    } else {
                        showStatus("processing", "Uploading" + statusText + " (resize failed, sending original)...");
                        doUpload(file.name, file);
                    }
                });
            } else {
                showStatus("processing", "Uploading" + statusText + "... (server will process)");
                doUpload(file.name, file);
            }
        }

        function doUpload(filename, blob, retryCount) {
            retryCount = retryCount || 0;
            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/upload/media", true);
            xhr.timeout = 600000;  // 10 minute timeout for large files

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                if (xhr.status === 200) {
                    completedFiles++;
                    showStatus("success", "Uploaded: " + filename + (uploadQueue.length > 0 ? " - continuing..." : ""));
                    processQueue();
                } else if (xhr.status === 409) {
                    skippedFiles.push(filename);
                    showStatus("processing", "Skipped (exists): " + filename + (uploadQueue.length > 0 ? " - continuing..." : ""));
                    processQueue();
                } else if (xhr.status === 413) {
                    // File too large
                    failedFiles.push({name: filename, error: "File too large (max 500MB)"});
                    showStatus("error", "Skipped: " + filename + " - file too large (max 500MB)");
                    processQueue();
                } else if (xhr.status === 503 && retryCount < 3) {
                    // Server busy - retry after delay
                    showStatus("processing", "Server busy, retrying " + filename + " in 5s... (attempt " + (retryCount + 2) + "/4)");
                    setTimeout(function() {
                        doUpload(filename, blob, retryCount + 1);
                    }, 5000);
                } else {
                    failedFiles.push({name: filename, error: xhr.responseText || "Unknown error"});
                    showStatus("error", "Failed: " + filename + " - " + (xhr.responseText || "Server error"));
                    processQueue();
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                if (retryCount < 2) {
                    // Network error - retry once
                    showStatus("processing", "Connection lost, retrying " + filename + "...");
                    setTimeout(function() {
                        doUpload(filename, blob, retryCount + 1);
                    }, 2000);
                } else {
                    failedFiles.push({name: filename, error: "Network error"});
                    showStatus("error", "Network error: " + filename + " (check connection)");
                    processQueue();
                }
            };

            xhr.ontimeout = function() {
                uploadZone.classList.remove("uploading");
                isUploading = false;
                failedFiles.push({name: filename, error: "Upload timed out"});
                showStatus("error", "Timeout: " + filename + " - upload took too long");
                processQueue();
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", encodeURIComponent(filename));
            xhr.send(blob);
        }
)HTML";

    // Part 11: Script - status and delete functions
    html += R"HTML(
        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }

        function deleteMedia(id) {
            if (!confirm("Delete this media?")) return;
            var xhr = new XMLHttpRequest();
            xhr.open("DELETE", "/api/media/personal/" + id, true);
            xhr.onload = function() {
                if (xhr.status === 200) {
                    location.reload();
                } else {
                    alert("Failed to delete media");
                }
            };
            xhr.send();
        }

        function deleteAllMedia(count) {
            if (!confirm("Delete all " + count + " personal media items? This cannot be undone.")) return;
            var xhr = new XMLHttpRequest();
            xhr.open("DELETE", "/api/media/personal", true);
            xhr.onload = function() {
                if (xhr.status === 200) {
                    location.reload();
                } else {
                    alert("Failed to delete media: " + xhr.responseText);
                }
            };
            xhr.send();
        }
    </script>
</body>
</html>
)HTML";

    return html;
}

void ShotServer::handleMediaUpload(QTcpSocket* socket, const QString& uploadedTempPath, const QString& headers)
{
    // Ensure temp file cleanup on any exit path
    QString tempPathToCleanup = uploadedTempPath;
    auto cleanupTempFile = [&tempPathToCleanup]() {
        if (!tempPathToCleanup.isEmpty() && QFile::exists(tempPathToCleanup)) {
            QFile::remove(tempPathToCleanup);
        }
    };

    try {
        if (!m_screensaverManager) {
            sendResponse(socket, 500, "text/plain", "Screensaver manager not available");
            cleanupTempFile();
            return;
        }

        // Get filename from X-Filename header (URL-encoded)
        QString filename = "uploaded_media";
        for (const QString& line : headers.split("\r\n")) {
            if (line.startsWith("X-Filename:", Qt::CaseInsensitive)) {
                filename = QUrl::fromPercentEncoding(line.mid(11).trimmed().toUtf8());
                break;
            }
        }

        // Validate file type
        QString ext = QFileInfo(filename).suffix().toLower();
        bool isImage = (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp");
        bool isVideo = (ext == "mp4" || ext == "webm" || ext == "mov");

        if (!isImage && !isVideo) {
            sendResponse(socket, 400, "text/plain", "Unsupported file type. Use JPG, PNG, GIF, WebP, MP4, or WebM.");
            cleanupTempFile();
            return;
        }

        // Check for duplicate before doing expensive resize work
        if (m_screensaverManager->hasPersonalMediaWithName(filename)) {
            sendResponse(socket, 409, "text/plain", "File already exists: " + filename.toUtf8());
            cleanupTempFile();
            return;
        }

        // Rename the streamed temp file to have proper extension
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempPath = tempDir + "/upload_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;

        if (!QFile::rename(uploadedTempPath, tempPath)) {
            // If rename fails (cross-device?), try copy
            if (!QFile::copy(uploadedTempPath, tempPath)) {
                sendResponse(socket, 500, "text/plain", "Failed to process uploaded file");
                cleanupTempFile();
                return;
            }
            QFile::remove(uploadedTempPath);
        }
        tempPathToCleanup = tempPath;  // Update cleanup path

        qDebug() << "Media uploaded to temp:" << tempPath << "size:" << QFileInfo(tempPath).size() << "bytes";

        // Extract date from original file BEFORE resizing (resize strips EXIF)
        QDateTime mediaDate;
        if (isImage) {
            mediaDate = extractImageDate(tempPath);
        } else if (isVideo) {
            mediaDate = extractVideoDate(tempPath);
        }

        // Resize the media
        QString outputPath = tempDir + "/resized_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;

        // Target resolution matches shared screensaver media (1280x800)
        const int targetWidth = 1280;
        const int targetHeight = 800;

        if (isImage) {
            if (resizeImage(tempPath, outputPath, targetWidth, targetHeight)) {
                QFile::remove(tempPath);
                tempPathToCleanup.clear();  // Successfully processed
                qDebug() << "Image resized successfully:" << outputPath;
            } else {
                // Use original if resize fails
                outputPath = tempPath;
                tempPathToCleanup.clear();  // Will use original, don't delete
                qDebug() << "Image resize failed, using original";
            }
        } else if (isVideo) {
            if (resizeVideo(tempPath, outputPath, targetWidth, targetHeight)) {
                QFile::remove(tempPath);
                tempPathToCleanup.clear();  // Successfully processed
                qDebug() << "Video resized successfully:" << outputPath;
            } else {
                // Use original if resize fails
                outputPath = tempPath;
                tempPathToCleanup.clear();  // Will use original, don't delete
                qDebug() << "Video resize not available or failed, using original";
            }
        }

        // Add to screensaver personal media with extracted date
        if (m_screensaverManager->addPersonalMedia(outputPath, filename, mediaDate)) {
            sendResponse(socket, 200, "text/plain", "Media uploaded successfully");
        } else {
            QFile::remove(outputPath);
            sendResponse(socket, 500, "text/plain", "Failed to add media to screensaver");
        }

    } catch (const std::exception& e) {
        qWarning() << "ShotServer: Exception in handleMediaUpload:" << e.what();
        cleanupTempFile();
        sendResponse(socket, 500, "text/plain", QString("Server error: %1").arg(e.what()).toUtf8());
    } catch (...) {
        qWarning() << "ShotServer: Unknown exception in handleMediaUpload";
        cleanupTempFile();
        sendResponse(socket, 500, "text/plain", "Server error: unexpected exception");
    }
}

bool ShotServer::resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight)
{
    try {
        QImage image(inputPath);
        if (image.isNull()) {
            qWarning() << "Failed to load image:" << inputPath;
            return false;
        }

        // Scale maintaining aspect ratio (fit within bounds)
        QImage scaled = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (scaled.isNull()) {
            qWarning() << "Failed to scale image (memory?):" << inputPath;
            return false;
        }

        // Create target-sized canvas with black background (letterbox/pillarbox)
        QImage result(maxWidth, maxHeight, QImage::Format_RGB32);
        result.fill(Qt::black);

        // Center the scaled image on the canvas using QPainter
        int x = (maxWidth - scaled.width()) / 2;
        int y = (maxHeight - scaled.height()) / 2;

        QPainter painter(&result);
        painter.drawImage(x, y, scaled);
        painter.end();

        // Save with good quality
        QString ext = QFileInfo(outputPath).suffix().toLower();
        if (ext == "jpg" || ext == "jpeg") {
            return result.save(outputPath, "JPEG", 85);
        } else if (ext == "png") {
            return result.save(outputPath, "PNG");
        } else {
            return result.save(outputPath);
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception in resizeImage:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception in resizeImage";
        return false;
    }
}

bool ShotServer::resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight)
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS - can't run FFmpeg
    Q_UNUSED(inputPath)
    Q_UNUSED(outputPath)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    qWarning() << "Video resizing not supported on iOS (no QProcess)";
    return false;
#else
    // Use FFmpeg for video resizing
    // FFmpeg command: ffmpeg -i input.mp4 -vf "scale='min(1920,iw)':min'(1080,ih)':force_original_aspect_ratio=decrease" -c:a copy output.mp4

    QString ffmpegPath = "ffmpeg";  // Assume it's in PATH

#ifdef Q_OS_WIN
    // On Windows, try common locations if not in PATH
    QStringList possiblePaths = {
        "ffmpeg",
        "C:/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        QCoreApplication::applicationDirPath() + "/ffmpeg.exe"
    };

    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffmpeg") {
            ffmpegPath = path;
            break;
        }
    }
#endif

    // Build filter chain: scale to fit, then pad with black to exact size (letterbox/pillarbox)
    QString filterChain = QString(
        "scale='min(%1,iw)':'min(%2,ih)':force_original_aspect_ratio=decrease,"
        "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black"
    ).arg(maxWidth).arg(maxHeight);

    QStringList args;
    args << "-y"  // Overwrite output
         << "-i" << inputPath
         << "-vf" << filterChain
         << "-c:v" << "libx264"
         << "-preset" << "fast"
         << "-crf" << "23"
         << "-c:a" << "aac"
         << "-b:a" << "128k"
         << outputPath;

    qDebug() << "Running FFmpeg:" << ffmpegPath << args.join(" ");

    QProcess process;
    process.start(ffmpegPath, args);

    if (!process.waitForStarted(5000)) {
        qWarning() << "FFmpeg failed to start. Is it installed?";
        return false;
    }

    // Wait up to 5 minutes for video processing
    if (!process.waitForFinished(300000)) {
        qWarning() << "FFmpeg timeout";
        process.kill();
        return false;
    }

    if (process.exitCode() != 0) {
        qWarning() << "FFmpeg error:" << process.readAllStandardError();
        return false;
    }

    qDebug() << "FFmpeg completed successfully";
    return QFile::exists(outputPath);
#endif
}

QDateTime ShotServer::extractDateWithExiftool(const QString& filePath) const
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(filePath)
    return QDateTime();
#else
    // Use exiftool for robust date extraction from any format
    QString exiftoolPath = "exiftool";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "exiftool",
        "C:/exiftool/exiftool.exe",
        "C:/Program Files/exiftool/exiftool.exe",
        QCoreApplication::applicationDirPath() + "/exiftool.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "exiftool") {
            exiftoolPath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-DateTimeOriginal" << "-CreateDate" << "-s3" << "-d" << "%Y-%m-%d %H:%M:%S" << filePath;

    QProcess process;
    process.start(exiftoolPath, args);

    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        return QDateTime();
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        // Take first non-empty line (DateTimeOriginal preferred)
        QString dateStr = output.split('\n').first().trimmed();
        QDateTime dt = QDateTime::fromString(dateStr, "yyyy-MM-dd HH:mm:ss");
        if (dt.isValid()) {
            qDebug() << "Exiftool extracted date:" << dt;
            return dt;
        }
    }

    return QDateTime();
#endif
}

QDateTime ShotServer::extractImageDate(const QString& imagePath) const
{
    // Try exiftool first (handles all formats including RAW/HEIC)
    QDateTime dt = extractDateWithExiftool(imagePath);
    if (dt.isValid()) {
        return dt;
    }

    // Fallback: try to extract EXIF DateTimeOriginal from JPEG files manually
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QDateTime();
    }

    QByteArray data = file.read(65536);  // Read first 64KB for EXIF
    file.close();

    // Check for JPEG magic bytes
    if (data.size() < 4 || (uchar)data[0] != 0xFF || (uchar)data[1] != 0xD8) {
        return QDateTime();  // Not a JPEG
    }

    // Search for EXIF marker (APP1 = 0xFFE1)
    int pos = 2;
    while (pos < data.size() - 4) {
        if ((uchar)data[pos] != 0xFF) {
            pos++;
            continue;
        }

        uchar marker = (uchar)data[pos + 1];
        if (marker == 0xE1) {  // APP1 (EXIF)
            int length = ((uchar)data[pos + 2] << 8) | (uchar)data[pos + 3];
            QByteArray exifData = data.mid(pos + 4, length - 2);

            // Check for "Exif\0\0" header
            if (exifData.startsWith("Exif\0\0")) {
                // Search for DateTimeOriginal tag (0x9003) in EXIF data
                // Format: "YYYY:MM:DD HH:MM:SS"
                QString exifStr = QString::fromLatin1(exifData);
                QRegularExpression dateRe("(\\d{4}):(\\d{2}):(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})");
                QRegularExpressionMatch match = dateRe.match(exifStr);
                if (match.hasMatch()) {
                    int year = match.captured(1).toInt();
                    int month = match.captured(2).toInt();
                    int day = match.captured(3).toInt();
                    int hour = match.captured(4).toInt();
                    int minute = match.captured(5).toInt();
                    int second = match.captured(6).toInt();

                    QDateTime dt(QDate(year, month, day), QTime(hour, minute, second));
                    if (dt.isValid() && year >= 1990 && year <= 2100) {
                        qDebug() << "Extracted EXIF date:" << dt;
                        return dt;
                    }
                }
            }
            break;
        } else if (marker == 0xD9 || marker == 0xDA) {
            break;  // End of image or start of scan
        } else if (marker >= 0xE0 && marker <= 0xEF) {
            // Skip other APP markers
            int length = ((uchar)data[pos + 2] << 8) | (uchar)data[pos + 3];
            pos += 2 + length;
        } else {
            pos += 2;
        }
    }

    return QDateTime();  // No date found
}

QDateTime ShotServer::extractVideoDate(const QString& videoPath) const
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(videoPath)
    return QDateTime();
#else
    // Use FFprobe to extract creation_time metadata
    QString ffprobePath = "ffprobe";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "ffprobe",
        "C:/ffmpeg/bin/ffprobe.exe",
        "C:/Program Files/ffmpeg/bin/ffprobe.exe",
        QCoreApplication::applicationDirPath() + "/ffprobe.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffprobe") {
            ffprobePath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-v" << "quiet"
         << "-print_format" << "json"
         << "-show_format"
         << videoPath;

    QProcess process;
    process.start(ffprobePath, args);

    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        return QDateTime();
    }

    QByteArray output = process.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        return QDateTime();
    }

    // Look for creation_time in format tags
    QJsonObject root = doc.object();
    QJsonObject format = root["format"].toObject();
    QJsonObject tags = format["tags"].toObject();

    QString creationTime = tags["creation_time"].toString();
    if (creationTime.isEmpty()) {
        creationTime = tags["com.apple.quicktime.creationdate"].toString();  // iOS
    }

    if (!creationTime.isEmpty()) {
        // Parse ISO 8601 format: "2024-01-15T10:30:00.000000Z"
        QDateTime dt = QDateTime::fromString(creationTime.left(19), "yyyy-MM-ddTHH:mm:ss");
        if (dt.isValid()) {
            qDebug() << "Extracted video date:" << dt;
            return dt;
        }
    }

    return QDateTime();
#endif
}
