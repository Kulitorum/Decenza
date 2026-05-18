#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include "widget/machinestatussnapshot.h"
#include "widget/widgetsharedkeys.h"
#include "ble/de1device.h"
#include "machine/machinestate.h"

// Unit tests for the machine-status widget snapshot. The producer side is the
// only part both mobile consumers depend on and the only part testable in the
// Qt Test framework; on-device widget rendering is covered by manual tasks.
// Uses DECENZA_TESTING friend access to drive private device/phase state.
class tst_MachineStatusSnapshot : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Keep snapshot file writes out of the real app data dir.
        QStandardPaths::setTestModeEnabled(true);
    }

    void schema_capturedAt_isoOffset() {
        WidgetSnapshot s;
        s.connected = true;
        s.phase = "Heating";
        s.temperatureC = 84.2;
        s.targetTemperatureC = 93.0;
        s.steamTemperatureC = 21.5;

        const QJsonObject o =
            QJsonDocument::fromJson(s.toJson()).object();

        QCOMPARE(o["schemaVersion"].toInt(), WidgetSharedKeys::kSchemaVersion);
        QCOMPARE(o["connected"].toBool(), true);
        QCOMPARE(o["phase"].toString(), QStringLiteral("Heating"));
        QVERIFY(o.contains("temperatureC"));
        QCOMPARE(o["temperatureC"].toDouble(), 84.2);
        QCOMPARE(o["targetTemperatureC"].toDouble(), 93.0);

        const QString captured = o["capturedAt"].toString();
        // ISO-8601 with explicit offset (or Z for UTC) — both mobile parsers
        // (Java OffsetDateTime, Swift ISO8601DateFormatter) require this.
        QRegularExpression re(
            QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}"
                            "([+-]\\d{2}:\\d{2}|Z)$"));
        QVERIFY2(re.match(captured).hasMatch(),
                 qPrintable("capturedAt not ISO-offset: " + captured));
        QVERIFY(QDateTime::fromString(captured, Qt::ISODate).isValid());
    }

    void lastShot_omittedWhenAbsent() {
        WidgetSnapshot s;
        const QJsonObject o = QJsonDocument::fromJson(s.toJson()).object();
        QVERIFY(!o.contains("lastShot"));   // absent, not null/empty
    }

    void lastShot_badgeOmittedWhenEmpty() {
        WidgetSnapshot s;
        s.lastShot = WidgetLastShot{ 36.1, 28.4, QString() };
        QJsonObject shot =
            QJsonDocument::fromJson(s.toJson()).object()["lastShot"].toObject();
        QCOMPARE(shot["yieldG"].toDouble(), 36.1);
        QCOMPARE(shot["durationSec"].toDouble(), 28.4);
        QVERIFY(!shot.contains("qualityBadge"));

        s.lastShot = WidgetLastShot{ 36.1, 28.4, QStringLiteral("channeling") };
        shot = QJsonDocument::fromJson(s.toJson())
                   .object()["lastShot"].toObject();
        QCOMPARE(shot["qualityBadge"].toString(),
                 QStringLiteral("channeling"));
    }

    void publishDisconnected_isHonest() {
        WidgetSnapshot s;   // defaults are the disconnected payload
        const QJsonObject o = QJsonDocument::fromJson(s.toJson()).object();
        QCOMPARE(o["connected"].toBool(), false);
        QCOMPARE(o["phase"].toString(), QStringLiteral("Disconnected"));
        QVERIFY(!o.contains("temperatureC"));
        QVERIFY(!o.contains("lastShot"));
        QVERIFY(QDateTime::fromString(o["capturedAt"].toString(),
                                      Qt::ISODate).isValid());
    }

    void setLastShot_rejectsNonFiniteAndNegative() {
        MachineStatusSnapshot snap(nullptr, nullptr);

        snap.setLastShot(std::nan(""), 28.0);
        QVERIFY(!snap.buildSnapshot().lastShot.has_value());

        snap.setLastShot(-1.0, 28.0);
        QVERIFY(!snap.buildSnapshot().lastShot.has_value());

        snap.setLastShot(36.0, -5.0);
        QVERIFY(!snap.buildSnapshot().lastShot.has_value());

        snap.setLastShot(36.1, 28.4);
        auto ls = snap.buildSnapshot().lastShot;
        QVERIFY(ls.has_value());
        QCOMPARE(ls->yieldG, 36.1);
        QCOMPARE(ls->durationSec, 28.4);
    }

    void temperatureCoalescing_onlyOnRoundedChange() {
        DE1Device device;
        MachineState state(&device);
        MachineStatusSnapshot snap(&device, &state);

        device.m_headTemp = 84.2;
        snap.onSampleReceived();
        QVERIFY(snap.m_lastTempKeyC.has_value());
        QCOMPARE(*snap.m_lastTempKeyC, 84);

        device.m_headTemp = 84.4;          // same rounded key → no change
        snap.onSampleReceived();
        QCOMPARE(*snap.m_lastTempKeyC, 84);

        device.m_headTemp = 84.6;          // crosses to 85 → flush
        snap.onSampleReceived();
        QCOMPARE(*snap.m_lastTempKeyC, 85);

        // Phase change resets the key so the new phase always flushes, and
        // the steam phase tracks steam temperature, not group temperature.
        snap.onPhaseChanged();
        QVERIFY(!snap.m_lastTempKeyC.has_value());

        state.m_phase = MachineState::Phase::Steaming;
        device.m_steamTemp = 135.3;
        snap.onSampleReceived();
        QVERIFY(snap.m_lastTempKeyC.has_value());
        QCOMPARE(*snap.m_lastTempKeyC, 135);
    }
};

QTEST_GUILESS_MAIN(tst_MachineStatusSnapshot)
#include "tst_machinestatussnapshot.moc"
