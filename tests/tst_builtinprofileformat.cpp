#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "profile/profile.h"
#include "network/visualizeruploader.h"
#include "history/shotprojection.h"

// Contract test for the canonical profile JSON format
// (OpenSpec: align-profile-json-with-reaprime).
//
// The Decent community needs a profile to make the same coffee in every app, so
// every profile Decenza emits must be readable by the strictest reader in the
// ecosystem — reaprime's Profile.fromJson, which hard-rejects a profile missing
// `tank_temperature` / `target_volume_count_start` or carrying an empty `steps`
// array. This runs the shipped built-ins through that contract, both as they sit
// on disk and after a load→serialize cycle.

static const QString BUILTIN_PROFILES_DIR =
    QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles");

class tst_BuiltinProfileFormat : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void builtinProfiles_data() {
        QTest::addColumn<QString>("filePath");

        QDir dir(BUILTIN_PROFILES_DIR);
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    // Every shipped built-in, as it sits on disk, satisfies reaprime's contract.
    void builtinProfiles() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(filePath));
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));

        const QStringList errors = Profile::reaprimeReadabilityErrors(doc.object());
        QVERIFY2(errors.isEmpty(), qPrintable(QFileInfo(filePath).fileName() + ": " + errors.join(", ")));
    }

    // A built-in that survives a load → canonical-serialize cycle is still
    // readable, and the serialized form is what an export/share would emit.
    void builtinProfilesRoundTripStayReadable_data() { builtinProfiles_data(); }

    void builtinProfilesRoundTripStayReadable() {
        QFETCH(QString, filePath);

        const Profile p = Profile::loadFromFile(filePath);
        QVERIFY2(p.isValid(), qPrintable(filePath));

        const QJsonObject out = p.toJsonObject();
        const QStringList errors = Profile::reaprimeReadabilityErrors(out);
        QVERIFY2(errors.isEmpty(), qPrintable(QFileInfo(filePath).fileName() + ": " + errors.join(", ")));

        // Canonical format: string-encoded values and the required aliases.
        QVERIFY(out.value("target_weight").isString());
        QCOMPARE(out.value("tank_temperature"), out.value("tank_desired_water_temperature"));
        QCOMPARE(out.value("target_volume_count_start"), out.value("number_of_preinfuse_frames"));
    }

    // The Visualizer upload carries the SAME canonical serialization as the
    // on-disk/exported profile — one format everywhere. Guards the delegation in
    // buildVisualizerProfileJson()/buildHistoryShotJson(): if either ever goes back
    // to hand-building its own payload, this diverges and fails.
    void visualizerUploadProfileIsCanonical() {
        const Profile p = Profile::loadFromFile(
            QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles/d_flow_default.json"));
        QVERIFY(p.isValid());

        ShotProjection proj;
        proj.pressure = QVariantList{};
        proj.profileJson = QString::fromUtf8(QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact));

        const QByteArray payload = VisualizerUploader::buildHistoryShotJson(proj);
        const QJsonObject uploaded = QJsonDocument::fromJson(payload).object()["profile"].toObject();

        QVERIFY2(!uploaded.isEmpty(), "upload payload carried no profile object");
        // Byte-identical to the canonical serialization of the same profile.
        QCOMPARE(QJsonDocument(uploaded).toJson(QJsonDocument::Compact),
                 QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact));
        // And it satisfies reaprime's reader contract.
        const QStringList errors = Profile::reaprimeReadabilityErrors(uploaded);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(", ")));
    }
};

QTEST_MAIN(tst_BuiltinProfileFormat)
#include "tst_builtinprofileformat.moc"
