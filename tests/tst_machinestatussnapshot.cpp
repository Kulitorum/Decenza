#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

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

    QString widgetDir() const {
        return QStandardPaths::writableLocation(
                   QStandardPaths::AppDataLocation)
               + QDir::separator()
               + QString::fromUtf8(WidgetSharedKeys::kDesktopSubdir);
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase() {
        // Keep snapshot writes out of the real app data dir, and guarantee
        // the desktop platformWrite path is writable so it never emits the
        // failure qWarning (project no-WARN rule, docs/CLAUDE_MD/TESTING.md).
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(QDir().mkpath(widgetDir()));
        QFileInfo fi(widgetDir());
        QVERIFY2(fi.isWritable(),
                 qPrintable("widget dir not writable: " + widgetDir()));
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

    void widgetLastShot_makeRejectsInvalid() {
        QVERIFY(!WidgetLastShot::make(std::nan(""), 28.0, {}).has_value());
        QVERIFY(!WidgetLastShot::make(36.0, -1.0, {}).has_value());
        QVERIFY(!WidgetLastShot::make(-2.0, 28.0, {}).has_value());
        auto ok = WidgetLastShot::make(36.1, 28.4, QStringLiteral("ok"));
        QVERIFY(ok.has_value());
        QCOMPARE(ok->yieldG, 36.1);
        QCOMPARE(ok->qualityBadge, QStringLiteral("ok"));
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

    void buildSnapshot_reflectsConnectedDevice() {
        DE1Device device;
        device.m_simulationMode = true;          // isConnected() → true
        device.m_headTemp = 92.0;
        device.m_goalTemperature = 93.0;
        device.m_steamTemp = 20.0;
        MachineState state(&device);
        state.m_phase = MachineState::Phase::Ready;

        MachineStatusSnapshot snap(&device, &state);
        const WidgetSnapshot s = snap.buildSnapshot();

        QVERIFY(s.connected);
        QCOMPARE(s.phase, QStringLiteral("Ready"));
        QVERIFY(s.temperatureC.has_value());
        QCOMPARE(*s.temperatureC, 92.0);
        QCOMPARE(*s.targetTemperatureC, 93.0);
    }

    void disconnectedSnapshot_omitsTemperatures() {
        DE1Device device;                        // not connected
        device.m_headTemp = 92.0;
        MachineState state(&device);
        MachineStatusSnapshot snap(&device, &state);
        const WidgetSnapshot s = snap.buildSnapshot();
        QVERIFY(!s.connected);
        QVERIFY(!s.temperatureC.has_value());    // honest: no temp when down
    }

    void setLastShot_rejectsNonFiniteAndNegative() {
        MachineStatusSnapshot snap(nullptr, nullptr);

        // The reject path warns exactly once per process by design — assert
        // it fires (and consume it so it doesn't trip the no-WARN rule).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            "\\[widget\\] setLastShot rejected non-finalized shot"));

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

    void desktopWrite_roundTrips() {
        MachineStatusSnapshot snap(nullptr, nullptr);
        snap.publishDisconnected();   // synchronous desktop file write

        const QString path = widgetDir() + QDir::separator()
            + QString::fromUtf8(WidgetSharedKeys::kDesktopFileName);

        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly),
                 qPrintable("snapshot file not written: " + path));
        const QJsonObject o =
            QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(o["connected"].toBool(), false);
        QCOMPARE(o["phase"].toString(), QStringLiteral("Disconnected"));
    }
};

QTEST_GUILESS_MAIN(tst_MachineStatusSnapshot)
#include "tst_machinestatussnapshot.moc"
