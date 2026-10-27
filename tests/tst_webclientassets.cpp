#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTimer>

#include "../src/network/webclientassets.h"
#include "../src/network/shotserver_webroute.h"
#include "../src/network/shotserveruploadroute.h"
#include "../src/network/webtemplates/auth_page.h"
#include "decenza_emoji_set.h"

class tst_WebClientAssets : public QObject
{
    Q_OBJECT
private:
    static QString packageId() { return QString(64, QLatin1Char('a')); }

    static QByteArray hash(const QByteArray& bytes)
    {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    }

    static bool writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    static QJsonObject fileEntry(const QByteArray& bytes)
    {
        return {{QStringLiteral("size"), bytes.size()},
                {QStringLiteral("sha256"), QString::fromLatin1(hash(bytes))}};
    }

    static bool writeManifest(const QString& root, const QJsonObject& entries,
                              const QString& identity = packageId())
    {
        QJsonObject manifest{
            {QStringLiteral("packageId"), identity},
            {QStringLiteral("files"), entries},
        };
        return writeFile(root + QStringLiteral("/package.json"),
                         QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    }

    static void seedBundle(const QString& root, QJsonObject* entries = nullptr)
    {
        const QMap<QString, QByteArray> assets{
            {QStringLiteral("index.html"), QByteArrayLiteral("<!doctype html><html><head></head><body></body></html>")},
            {QStringLiteral("decenza-web.js"), QByteArrayLiteral("export {};")},
            {QStringLiteral("decenza-web.wasm"), QByteArrayLiteral("wasm-bytes")},
            {QStringLiteral("qtloader.js"), QByteArrayLiteral("globalThis.qt = {};")},
            {QStringLiteral("qt-dialog-accessibility.js"), QByteArrayLiteral("globalThis.a11y = true;")},
        };
        QJsonObject files;
        for (auto it = assets.cbegin(); it != assets.cend(); ++it) {
            if (!writeFile(root + QLatin1Char('/') + it.key(), it.value()))
                qFatal("failed to write test asset");
            files.insert(it.key(), fileEntry(it.value()));
        }
        if (entries)
            *entries = files;
        if (!writeManifest(root, files))
            qFatal("failed to write test manifest");
    }

    static QString cachePrefix(const QString& root, const QString& identity = packageId())
    {
        const auto entry = readWebClientAsset(root, QStringLiteral("/wasm/"), identity,
            {}, QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1"));
        const auto base = QRegularExpression(QStringLiteral("<base href=\"([^\"]+)\">"))
            .match(QString::fromUtf8(entry.body));
        if (entry.statusCode != 200 || !base.hasMatch())
            qFatal("test bootstrap has no asset base");
        return base.captured(1);
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void embeddedBundleUsesTheSameAllowlistIdentityAndIntegrityChecks()
    {
        const QString root = QStringLiteral(":/webclient");
        const auto entry = readWebClientAsset(root, QStringLiteral("/wasm/"), packageId());
        QCOMPARE(entry.statusCode, 200);
        QVERIFY(entry.body.contains("embedded"));
        QCOMPARE(readWebClientAsset(root, QStringLiteral("/wasm/decenza-web.wasm"),
                                     packageId()).body, QByteArrayLiteral("wasm-fixture\n"));
        QCOMPARE(readWebClientAsset(root, QStringLiteral("/wasm/package.json"),
                                     packageId()).statusCode, 404);
        QCOMPARE(readWebClientAsset(root, QStringLiteral("/wasm/../index.html"),
                                     packageId()).statusCode, 404);
        QCOMPARE(readWebClientAsset(root, QStringLiteral("/wasm/"),
                                     QString(64, QLatin1Char('b'))).statusCode, 409);
        // The fixture script differs from its manifest while the HTML is valid.
        QCOMPARE(readWebClientAsset(root, QStringLiteral("/wasm/decenza-web.js"),
                                     packageId()).statusCode, 503);
    }

    void servesOnlyAllowlistedAssetsWithTheirMimeTypes()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());

        const QList<QPair<QString, QString>> cases{
            {QStringLiteral("index.html"), QStringLiteral("text/html; charset=utf-8")},
            {QStringLiteral("decenza-web.js"), QStringLiteral("text/javascript; charset=utf-8")},
            {QStringLiteral("decenza-web.wasm"), QStringLiteral("application/wasm")},
            {QStringLiteral("qtloader.js"), QStringLiteral("text/javascript; charset=utf-8")},
            {QStringLiteral("qt-dialog-accessibility.js"), QStringLiteral("text/javascript; charset=utf-8")},
        };
        for (const auto& testCase : cases) {
            const WebClientAssetResult result = readWebClientAsset(
                bundle.path(), QStringLiteral("/wasm/") + testCase.first, packageId());
            QCOMPARE(result.statusCode, 200);
            QCOMPARE(result.contentType, testCase.second);
            QVERIFY(!result.body.isEmpty());
            QVERIFY(result.extraHeaders.contains("Cache-Control: no-store"));
            QVERIFY(result.extraHeaders.contains("X-Content-Type-Options: nosniff"));
        }
    }

    void entryBootstrapUsesTheCurrentHostEpochAfterIntegrityValidation()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());
        const QString firstHost = QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1");
        const QString nextHost = QStringLiteral("9a54d42f-93de-48a7-ae78-38925bd0c3e1");
        const auto first = readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"),
                                              packageId(), "br", firstHost);
        QCOMPARE(first.statusCode, 200);
        QVERIFY(first.body.contains(QByteArrayLiteral("name=\"decenza-session-package\"")));
        QVERIFY(first.body.contains(packageId().toUtf8()));
        QVERIFY(first.body.contains("<base href=\"" + cachePrefix(bundle.path()).toUtf8() + "\">"));
        QVERIFY(first.body.contains(firstHost.toUtf8()));
        QVERIFY(first.extraHeaders.contains("Cache-Control: no-store"));
        QVERIFY(!first.extraHeaders.contains("Content-Encoding"));
        QVERIFY(!first.extraHeaders.contains("ETag"));

        const auto root = readWebClientAsset(bundle.path(),
            ShotServerWebRoute::wasmRootTarget(QStringLiteral("/")), packageId(), "br", firstHost);
        QCOMPARE(root.body, first.body);
        const auto restarted = readWebClientAsset(bundle.path(), QStringLiteral("/wasm/index.html"),
                                                  packageId(), "br", nextHost);
        QCOMPARE(restarted.statusCode, 200);
        QVERIFY(restarted.body.contains(nextHost.toUtf8()));
        QVERIFY(!restarted.body.contains(firstHost.toUtf8()));
        QVERIFY(restarted.body != first.body);
        QCOMPARE(readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"), packageId(),
                                     "identity;q=0,br", nextHost).statusCode, 406);
        QCOMPARE(readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"), packageId(), {},
                                     QStringLiteral("\"><script>bad</script>")).statusCode, 503);

        QVERIFY(writeFile(bundle.path() + QStringLiteral("/index.html"),
                           QByteArrayLiteral("<head>tampered</head>")));
        const auto tampered = readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"),
                                                 packageId(), {}, firstHost);
        QCOMPARE(tampered.statusCode, 503);
        QVERIFY(!tampered.body.contains("decenza-session-host"));
    }

    void versionedAssetsFollowThePackagedBytes()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        QJsonObject files;
        seedBundle(bundle.path(), &files);
        const QString prefix = cachePrefix(bundle.path());
        QVERIFY(prefix != QStringLiteral("/wasm/") + packageId() + QLatin1Char('/'));
        for (const QString& name : {QStringLiteral("decenza-web.js"), QStringLiteral("decenza-web.wasm"),
                                    QStringLiteral("qtloader.js"), QStringLiteral("qt-dialog-accessibility.js")}) {
            const auto result = readWebClientAsset(bundle.path(), prefix + name, packageId());
            QCOMPARE(result.statusCode, 200);
            QVERIFY(result.extraHeaders.contains("Cache-Control: private, max-age=31536000, immutable"));
            QCOMPARE(result.body, readWebClientAsset(bundle.path(), QStringLiteral("/wasm/") + name,
                                                     packageId()).body);
        }
        const auto entry = readWebClientAsset(bundle.path(), prefix + QStringLiteral("index.html"), packageId());
        QCOMPARE(entry.statusCode, 200);
        QVERIFY(entry.extraHeaders.contains("Cache-Control: no-store"));

        const QString nextPackage(64, QLatin1Char('b'));
        QVERIFY(writeManifest(bundle.path(), files, nextPackage));
        const QString nextPrefix = cachePrefix(bundle.path(), nextPackage);
        const auto oldAsset = readWebClientAsset(bundle.path(), prefix + QStringLiteral("decenza-web.wasm"),
                                                  nextPackage);
        QCOMPARE(oldAsset.statusCode, 404);
        QVERIFY(oldAsset.extraHeaders.contains("Cache-Control: no-store"));
        const auto nextAsset = readWebClientAsset(bundle.path(), nextPrefix + QStringLiteral("decenza-web.wasm"),
                                                   nextPackage);
        QCOMPARE(nextAsset.statusCode, 200);
        const auto nextEntry = readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"), nextPackage,
            {}, QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1"));
        QVERIFY(nextEntry.body.contains("<base href=\"" + nextPrefix.toUtf8() + "\">"));
        QVERIFY(!nextEntry.body.contains(prefix.toUtf8()));

        // A toolchain/optimization rebuild can change bytes without changing source identity.
        const QByteArray rebuiltWasm = QByteArrayLiteral("rebuilt-wasm-bytes");
        QVERIFY(writeFile(bundle.path() + QStringLiteral("/decenza-web.wasm"), rebuiltWasm));
        files.insert(QStringLiteral("decenza-web.wasm"), fileEntry(rebuiltWasm));
        QVERIFY(writeManifest(bundle.path(), files, nextPackage));
        const auto rebuiltEntry = readWebClientAsset(bundle.path(), QStringLiteral("/wasm/"), nextPackage,
            {}, QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1"));
        QCOMPARE(rebuiltEntry.statusCode, 200);
        QVERIFY2(rebuiltEntry.body != nextEntry.body,
                 "Rebuilt asset bytes must change the cache base even for the same package");
        const QString rebuiltPrefix = cachePrefix(bundle.path(), nextPackage);
        const auto oldBuild = readWebClientAsset(bundle.path(), nextPrefix + QStringLiteral("decenza-web.wasm"),
                                                  nextPackage);
        QCOMPARE(oldBuild.statusCode, 404);
        QVERIFY(oldBuild.extraHeaders.contains("Cache-Control: no-store"));
        QCOMPARE(readWebClientAsset(bundle.path(), rebuiltPrefix + QStringLiteral("decenza-web.wasm"),
                                      nextPackage).body, rebuiltWasm);
    }

    void queryParametersDoNotChangeAllowlistedAssetRouting()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());

        const WebClientAssetResult root = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/?accessibility-repro=list-item"), packageId());
        QCOMPARE(root.statusCode, 200);
        QCOMPARE(root.contentType, QStringLiteral("text/html; charset=utf-8"));
        QVERIFY(root.body.startsWith(QByteArrayLiteral("<!doctype html>")));

        const WebClientAssetResult defaultRoute = readWebClientAsset(
            bundle.path(), ShotServerWebRoute::wasmRootTarget(QStringLiteral("/")), packageId());
        QCOMPARE(defaultRoute.statusCode, 200);
        QCOMPARE(defaultRoute.body, root.body);

        const WebClientAssetResult asset = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.js?build=smoke"), packageId());
        QCOMPARE(asset.statusCode, 200);
        QCOMPARE(asset.body, QByteArrayLiteral("export {};"));

        const WebClientAssetResult unknown = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/not-allowlisted.js?build=smoke"), packageId());
        QCOMPARE(unknown.statusCode, 404);
    }

    void legacyPrefixAliasesExistingPagesAndUploads()
    {
        QVERIFY(ShotServerWebRoute::isLegacyPath(QStringLiteral("/legacy")));
        QVERIFY(ShotServerWebRoute::isLegacyPath(QStringLiteral("/legacy/recipes?q=coffee")));
        QVERIFY(!ShotServerWebRoute::isLegacyPath(QStringLiteral("/legacy-tools")));
        QCOMPARE(ShotServerWebRoute::legacyTarget(QStringLiteral("/legacy")), QStringLiteral("/"));
        QCOMPARE(ShotServerWebRoute::legacyTarget(QStringLiteral("/legacy/?q=coffee")),
                 QStringLiteral("/?q=coffee"));
        QCOMPARE(ShotServerWebRoute::legacyTarget(QStringLiteral("/legacy/recipes?q=coffee")),
                 QStringLiteral("/recipes?q=coffee"));
        QCOMPARE(ShotServerWebRoute::legacyTarget(QStringLiteral("/legacy/shot/123/profile.json")),
                 QStringLiteral("/shot/123/profile.json"));
        QCOMPARE(ShotServerWebRoute::legacyTarget(QStringLiteral("/recipes")),
                 QStringLiteral("/recipes"));
        QCOMPARE(ShotServerWebRoute::wasmRootTarget(QStringLiteral("/")),
                 QStringLiteral("/wasm/"));
        QCOMPARE(ShotServerWebRoute::wasmRootTarget(QStringLiteral("/?source=bookmark")),
                 QStringLiteral("/wasm/?source=bookmark"));
        QCOMPARE(ShotServerWebRoute::wasmRootTarget(QStringLiteral("/wasm/")),
                 QStringLiteral("/wasm/"));

        QCOMPARE(int(streamedUploadKind(QStringLiteral("POST /legacy/upload HTTP/1.1"))),
                 int(StreamedUpload::Apk));
        QCOMPARE(int(streamedUploadKind(QStringLiteral("POST /legacy/upload/media HTTP/1.1"))),
                 int(StreamedUpload::Media));
        QCOMPARE(int(streamedUploadKind(QStringLiteral("POST /legacy/api/backup/restore HTTP/1.1"))),
                 int(StreamedUpload::BackupRestore));
    }

    void authLoginReturnsToTheSameLegacyRequest()
    {
        const QByteArray loginPage(WEB_AUTH_LOGIN_PAGE);
        QVERIFY(loginPage.contains("window.location.reload()"));
        QVERIFY(loginPage.contains("path === '/legacy/auth/login'"));
        QVERIFY(loginPage.contains("window.location.assign('/')"));
    }

    void liveShotServerRoutesAreReadOnlyWhenOptedIn()
    {
        const QByteArray configuredUrl = qgetenv("DECENZA_SHOTSERVER_ROUTE_TEST_URL");
        if (configuredUrl.isEmpty())
            QSKIP("Set DECENZA_SHOTSERVER_ROUTE_TEST_URL to an explicitly selected local ShotServer");

        const QUrl baseUrl(QString::fromUtf8(configuredUrl));
        QVERIFY2(baseUrl.isValid(), "route-test URL must be a valid URL");
        if (baseUrl.scheme() != QLatin1String("http"))
            QSKIP("This read-only integration probe intentionally uses plain localhost HTTP");
        QVERIFY(baseUrl.userName().isEmpty());
        QVERIFY(baseUrl.password().isEmpty());
        QVERIFY(baseUrl.path().isEmpty() || baseUrl.path() == QLatin1String("/"));
        QVERIFY(!baseUrl.hasQuery());
        QVERIFY(!baseUrl.hasFragment());
        QHostAddress address;
        const bool loopback = baseUrl.host() == QLatin1String("localhost")
            || (address.setAddress(baseUrl.host()) && address.isLoopback());
        QVERIFY2(loopback, "route-test URL must target localhost or a loopback IP");
        QVERIFY(baseUrl.port() > 0);

        struct Response {
            int status = 0;
            QByteArray location;
            QByteArray contentType;
            QByteArray contentDisposition;
            QByteArray body;
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        const auto get = [&manager, &baseUrl](const QString& path) {
            QNetworkRequest request(baseUrl.resolved(QUrl(path)));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            QNetworkReply* reply = manager.get(request);
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(5000);
            loop.exec();

            Response response;
            if (reply->isFinished()) {
                response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                response.location = reply->rawHeader(QByteArrayLiteral("Location"));
                response.contentType = reply->rawHeader(QByteArrayLiteral("Content-Type"));
                response.contentDisposition = reply->rawHeader(QByteArrayLiteral("Content-Disposition"));
                response.body = reply->readAll();
            } else {
                reply->abort();
            }
            reply->deleteLater();
            return response;
        };

        const Response root = get(QStringLiteral("/?route_probe=1"));
        if (root.status == 401)
            QSKIP("The selected host requires authentication; this probe does not alter auth state");
        QVERIFY2(root.status == 200 || root.status == 503,
                 "the root entry must serve the verified Wasm bundle or report it unavailable");
        QVERIFY(root.location.isEmpty());
        if (root.status == 200)
            QVERIFY(root.contentType.startsWith("text/html"));
        else
            QVERIFY(root.body.contains("Packaged web client"));

        const Response oldIndex = get(QStringLiteral("/index.html?route_probe=1"));
        QCOMPARE(oldIndex.status, 302);
        QCOMPARE(oldIndex.location, QByteArrayLiteral("/legacy/?route_probe=1"));

        const Response wasmAlias = get(QStringLiteral("/wasm"));
        QCOMPARE(wasmAlias.status, 302);
        QCOMPARE(wasmAlias.location, QByteArrayLiteral("/wasm/"));

        const Response wasmEntry = get(QStringLiteral("/wasm/"));
        QVERIFY2(wasmEntry.status == 200 || wasmEntry.status == 503,
                 "the Wasm entry must either serve the verified bundle or report it unavailable");
        QVERIFY(wasmEntry.location.isEmpty());
        if (wasmEntry.status == 200)
            QVERIFY(wasmEntry.contentType.startsWith("text/html"));
        else
            QVERIFY(wasmEntry.body.contains("Packaged web client"));
        if (root.status == 200 && wasmEntry.status == 200)
            QCOMPARE(root.body, wasmEntry.body);

        const Response legacyList = get(QStringLiteral("/legacy/"));
        QCOMPARE(legacyList.status, 200);
        QVERIFY(legacyList.contentType.startsWith("text/html"));
        QVERIFY(legacyList.body.contains("Shot History"));

        const Response legacySettings = get(QStringLiteral("/legacy/settings?route_probe=1"));
        QCOMPARE(legacySettings.status, 200);
        QVERIFY(legacySettings.contentType.startsWith("text/html"));
        QVERIFY(legacySettings.location.isEmpty());

        const Response legacyUpload = get(QStringLiteral("/legacy/upload"));
        QCOMPARE(legacyUpload.status, 200);
        QVERIFY(legacyUpload.contentType.startsWith("text/html"));
        QVERIFY(legacyUpload.location.isEmpty());

        const Response legacyMediaUpload = get(QStringLiteral("/legacy/upload/media"));
        QCOMPARE(legacyMediaUpload.status, 200);
        QVERIFY(legacyMediaUpload.contentType.startsWith("text/html"));
        QVERIFY(legacyMediaUpload.location.isEmpty());

        // Old page bookmarks remain usable as aliases; API reads stay at the
        // original root and are never redirected into either UI namespace.
        const Response oldRecipeBookmark = get(QStringLiteral("/recipes?restock=1&recipe=2"));
        QCOMPARE(oldRecipeBookmark.status, 200);
        QVERIFY(oldRecipeBookmark.contentType.startsWith("text/html"));
        QVERIFY(oldRecipeBookmark.location.isEmpty());

        const Response legacyRecipeBookmark = get(QStringLiteral("/legacy/recipes?restock=1&recipe=2"));
        QCOMPARE(legacyRecipeBookmark.status, 200);
        QVERIFY(legacyRecipeBookmark.contentType.startsWith("text/html"));
        QVERIFY(legacyRecipeBookmark.location.isEmpty());

        const Response legacyBeanBookmark = get(QStringLiteral("/legacy/beans?restock=1&recipe=2"));
        QCOMPARE(legacyBeanBookmark.status, 200);
        QVERIFY(legacyBeanBookmark.contentType.startsWith("text/html"));
        QVERIFY(legacyBeanBookmark.location.isEmpty());

        const Response api = get(QStringLiteral("/api/shots"));
        QCOMPARE(api.status, 200);
        QVERIFY(api.contentType.startsWith("application/json"));
        QVERIFY(api.location.isEmpty());

        const Response missingShot = get(QStringLiteral("/legacy/shot/9223372036854775807"));
        QCOMPARE(missingShot.status, 404);
        QVERIFY(missingShot.contentType.startsWith("text/html"));
        QVERIFY(missingShot.body.contains("href=\"/legacy/\""));

        const QJsonDocument shotsDocument = QJsonDocument::fromJson(api.body);
        QVERIFY(shotsDocument.isArray());
        const QJsonArray shots = shotsDocument.array();
        if (shots.isEmpty())
            QSKIP("The route probe needs one existing shot for read-only detail and download checks");
        const QJsonObject firstShot = shots.first().toObject();
        bool shotIdOk = false;
        const qint64 shotId = firstShot.value(QStringLiteral("id")).toVariant().toLongLong(&shotIdOk);
        QVERIFY(shotIdOk);
        QVERIFY(shotId > 0);

        const QString shotPath = QStringLiteral("/legacy/shot/%1").arg(shotId);
        const Response detail = get(shotPath);
        QCOMPARE(detail.status, 200);
        QVERIFY(detail.contentType.startsWith("text/html"));
        QVERIFY(detail.location.isEmpty());

        // This small visualizer-format download is read-only and confirms the
        // root-level file route remains outside the /legacy/ namespace.
        const Response shotDownload = get(QStringLiteral("/shot/%1/shot.json?route_probe=1").arg(shotId));
        QCOMPARE(shotDownload.status, 200);
        QVERIFY(shotDownload.contentType.startsWith("application/json"));
        QVERIFY(shotDownload.contentDisposition.startsWith("attachment; filename="));
        QVERIFY(shotDownload.location.isEmpty());
        QVERIFY(shotDownload.body.startsWith("{"));
        QVERIFY(shotDownload.body.size() < 2 * 1024 * 1024);
    }

    void rejectsUnknownAndTraversalPaths()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());
        for (const QString& path : {QStringLiteral("/wasm/../package.json"),
                                    QStringLiteral("/wasm/secret.txt"),
                                    QStringLiteral("/wasm/../package.json?x=1"),
                                    QStringLiteral("/other/index.html")}) {
            const WebClientAssetResult result = readWebClientAsset(bundle.path(), path, packageId());
            QCOMPARE(result.statusCode, 404);
            QVERIFY(result.body.isEmpty() == false);
        }
    }

    void rejectsPackageMismatchAndMalformedManifest()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());
        const WebClientAssetResult mismatch = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/index.html"), QString(64, QLatin1Char('b')));
        QCOMPARE(mismatch.statusCode, 409);

        QVERIFY(writeFile(bundle.path() + QStringLiteral("/package.json"), QByteArrayLiteral("{")));
        const WebClientAssetResult malformed = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/index.html"), packageId());
        QCOMPARE(malformed.statusCode, 503);
    }

    void verifiesSizeAndDigestBeforeServing()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        seedBundle(bundle.path());
        QVERIFY(writeFile(bundle.path() + QStringLiteral("/index.html"), QByteArrayLiteral("changed")));
        const WebClientAssetResult changed = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/index.html"), packageId());
        QCOMPARE(changed.statusCode, 503);

        QJsonObject files;
        files.insert(QStringLiteral("index.html"), QJsonObject{
            {QStringLiteral("size"), 10},
            {QStringLiteral("sha256"), QString::fromLatin1(hash("x"))},
        });
        QVERIFY(writeManifest(bundle.path(), files));
        const WebClientAssetResult oversized = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/index.html"), packageId());
        QCOMPARE(oversized.statusCode, 503);

        files.insert(QStringLiteral("index.html"), QJsonObject{
            {QStringLiteral("size"), 10 * 1024 * 1024 + 1},
            {QStringLiteral("sha256"), QString::fromLatin1(hash("x"))},
        });
        QVERIFY(writeManifest(bundle.path(), files));
        const WebClientAssetResult overLimit = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/index.html"), packageId());
        QCOMPARE(overLimit.statusCode, 503);
    }

    void servesVerifiedBrotliOnlyWhenAccepted()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        QJsonObject files;
        seedBundle(bundle.path(), &files);
        const QByteArray brotliBytes = QByteArray::fromHex("8b0280776173");
        QVERIFY(writeFile(bundle.path() + QStringLiteral("/decenza-web.wasm.br"), brotliBytes));
        QJsonObject wasmEntry = files.value(QStringLiteral("decenza-web.wasm")).toObject();
        wasmEntry.insert(QStringLiteral("brSize"), brotliBytes.size());
        wasmEntry.insert(QStringLiteral("brSha256"), QString::fromLatin1(hash(brotliBytes)));
        files.insert(QStringLiteral("decenza-web.wasm"), wasmEntry);
        QVERIFY(writeManifest(bundle.path(), files));

        const WebClientAssetResult brotli = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.wasm"), packageId(),
            QByteArrayLiteral("gzip, deflate, br, identity;q=0.5"));
        QCOMPARE(brotli.statusCode, 200);
        QCOMPARE(brotli.body, brotliBytes);
        QVERIFY(brotli.extraHeaders.contains("Content-Encoding: br"));
        QVERIFY(brotli.extraHeaders.contains("Vary: Accept-Encoding"));

        const auto cachedBrotli = readWebClientAsset(bundle.path(),
            cachePrefix(bundle.path()) + QStringLiteral("decenza-web.wasm"),
            packageId(), QByteArrayLiteral("br"));
        QCOMPARE(cachedBrotli.statusCode, 200);
        QCOMPARE(cachedBrotli.body, brotliBytes);
        QVERIFY(cachedBrotli.extraHeaders.contains("Content-Encoding: br"));
        QVERIFY(cachedBrotli.extraHeaders.contains("Cache-Control: private, max-age=31536000, immutable"));

        // A browser offering only gzip gets the original: Brotli is the one packaged encoding.
        const WebClientAssetResult gzipOnly = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.wasm"), packageId(),
            QByteArrayLiteral("gzip, deflate"));
        QCOMPARE(gzipOnly.statusCode, 200);
        QCOMPARE(gzipOnly.body, QByteArrayLiteral("wasm-bytes"));
        QVERIFY(!gzipOnly.extraHeaders.contains("Content-Encoding"));

        const WebClientAssetResult identity = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.wasm"), packageId(),
            QByteArrayLiteral("br;q=0, *;q=0.2"));
        QCOMPARE(identity.statusCode, 200);
        QCOMPARE(identity.body, QByteArrayLiteral("wasm-bytes"));
        QVERIFY(!identity.extraHeaders.contains("Content-Encoding"));

        const WebClientAssetResult duplicateCoding = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.wasm"), packageId(),
            QByteArrayLiteral("br;q=0, br"));
        QCOMPARE(duplicateCoding.statusCode, 200);
        QCOMPARE(duplicateCoding.body, QByteArrayLiteral("wasm-bytes"));
        QVERIFY(!duplicateCoding.extraHeaders.contains("Content-Encoding"));

        const WebClientAssetResult notAcceptable = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.wasm"), packageId(),
            QByteArrayLiteral("br;q=0, identity;q=0, *;q=0"));
        QCOMPARE(notAcceptable.statusCode, 406);
    }

    void servesBundledEmojiOnlyAtTheCurrentSetUrl()
    {
        const QString base = QStringLiteral("/wasm/emoji/" DECENZA_EMOJI_SET_HASH "/");
        QFile expected(QStringLiteral(":/emoji/2615.svg"));
        QVERIFY(expected.open(QIODevice::ReadOnly));

        const WebClientAssetResult coffee = readWebClientEmoji(base + QStringLiteral("2615.svg"));
        QCOMPARE(coffee.statusCode, 200);
        QCOMPARE(coffee.contentType, QStringLiteral("image/svg+xml"));
        QCOMPARE(coffee.body, expected.readAll());
        QVERIFY(coffee.extraHeaders.contains("Cache-Control: private, max-age=31536000, immutable"));
        QVERIFY(coffee.extraHeaders.contains("X-Content-Type-Options: nosniff"));
        QCOMPARE(readWebClientEmoji(base + QStringLiteral("31-20e3.svg")).statusCode, 200);

        const QStringList refused{
            base + QStringLiteral("1f322.svg"),                      // no upstream image
            QStringLiteral("/wasm/emoji/") + QString(64, QLatin1Char('0'))
                + QStringLiteral("/2615.svg"),                       // another build's set
            base + QStringLiteral("2615.png"),
            base + QStringLiteral("2615"),
            base + QStringLiteral("2615.svg/x.svg"),
            base + QStringLiteral("..%2F..%2Fwebclient%2Findex.html"),
            base + QStringLiteral("../2615.svg"),
            base + QStringLiteral("2615-.svg"),
        };
        for (const QString& path : refused)
            QCOMPARE(readWebClientEmoji(path).statusCode, 404);
    }

    void rejectsBrotliIntegrityFailureInsteadOfServingIt()
    {
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        QJsonObject files;
        seedBundle(bundle.path(), &files);
        const QByteArray brotliBytes = QByteArrayLiteral("not-the-declared-file");
        QVERIFY(writeFile(bundle.path() + QStringLiteral("/decenza-web.js.br"), brotliBytes));
        QJsonObject jsEntry = files.value(QStringLiteral("decenza-web.js")).toObject();
        jsEntry.insert(QStringLiteral("brSize"), brotliBytes.size());
        jsEntry.insert(QStringLiteral("brSha256"), QString(64, QLatin1Char('0')));
        files.insert(QStringLiteral("decenza-web.js"), jsEntry);
        QVERIFY(writeManifest(bundle.path(), files));

        const WebClientAssetResult result = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.js"), packageId(),
            QByteArrayLiteral("br, identity;q=0.5"));
        QCOMPARE(result.statusCode, 503);
    }

    void rejectsSymlinkThatEscapesBundle()
    {
        QTemporaryDir bundle;
        QTemporaryDir outside;
        QVERIFY(bundle.isValid());
        QVERIFY(outside.isValid());
        seedBundle(bundle.path());
        const QByteArray secret = QByteArrayLiteral("outside bundle");
        QVERIFY(writeFile(outside.path() + QStringLiteral("/external.js"), secret));
        QFile::remove(bundle.path() + QStringLiteral("/decenza-web.js"));
        if (!QFile::link(outside.path() + QStringLiteral("/external.js"),
                         bundle.path() + QStringLiteral("/decenza-web.js"))) {
            QSKIP("platform does not allow creating a test symlink");
        }
        QJsonObject files{
            {QStringLiteral("decenza-web.js"), fileEntry(secret)},
        };
        QVERIFY(writeManifest(bundle.path(), files));
        const WebClientAssetResult result = readWebClientAsset(
            bundle.path(), QStringLiteral("/wasm/decenza-web.js"), packageId());
        QCOMPARE(result.statusCode, 503);
    }
};

QTEST_GUILESS_MAIN(tst_WebClientAssets)
#include "tst_webclientassets.moc"
