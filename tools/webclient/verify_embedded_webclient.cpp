#include "decenza_package_identity.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cstdio>

namespace {

QByteArray fileHash(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || !hash.addData(&file))
        return {};
    return hash.result();
}

int fail(const QString& message)
{
    QTextStream(stderr) << "Embedded web client verification failed: " << message << Qt::endl;
    return 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
        return fail(QStringLiteral("Usage: decenza-verify-webclient <deployment-directory>"));

    const QDir deployment(app.arguments().at(1));
    const QStringList files = deployment.entryList(QDir::Files, QDir::Name);
    if (!files.contains(QStringLiteral("package.json"))
        || !files.contains(QStringLiteral("decenza-web.wasm"))) {
        return fail(QStringLiteral("deployment must contain package.json and decenza-web.wasm"));
    }

    QFile manifest(deployment.filePath(QStringLiteral("package.json")));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 64 * 1024)
        return fail(QStringLiteral("package.json is unreadable or too large"));
    const auto package = QJsonDocument::fromJson(manifest.readAll()).object();
    if (package.value(QStringLiteral("packageId")).toString() != QString::fromLatin1(DECENZA_PACKAGE_ID))
        return fail(QStringLiteral("package.json does not match the compiled package identity"));

    // Rely on the carrier's normal resource initialization, just as Decenza does.
    for (const QString& name : files) {
        const QByteArray expected = fileHash(deployment.filePath(name));
        const QByteArray embedded = fileHash(QStringLiteral(":/webclient/") + name);
        if (expected.isEmpty() || embedded != expected)
            return fail(QStringLiteral("missing, empty or mismatched embedded asset: %1").arg(name));
    }
    QTextStream(stdout) << "Verified " << files.size()
                        << " deployment files against embedded Qt resources" << Qt::endl;
    return 0;
}
