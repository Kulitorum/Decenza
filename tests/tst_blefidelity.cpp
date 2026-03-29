#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "profile/profile.h"
#include "profile/profileframe.h"
#include "ble/protocol/binarycodec.h"
#include "ble/protocol/de1characteristics.h"

// Test BLE fidelity: load every built-in JSON profile, encode to BLE bytes,
// verify header/frame/tail structure, and round-trip encoded values back through
// the codec to catch silent corruption.
//
// Rationale: The TargetEspressoVol=36 bug was in the BLE encoding layer. Testing
// encode→decode round-trip for all profiles catches this class of bug.
//
// Expected values derived from de1app binary.tcl encoding routines.

static const QString kProfilesDir = QStringLiteral(PROFILES_DIR);

class tst_BleFidelity : public QObject {
    Q_OBJECT

private:
    static Profile loadProfile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return Profile();
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        return Profile::fromJson(doc);
    }

    // Populate test rows with profiles that have encodable frames.
    // Excludes wizard/utility profiles (e.g. descale_wizard) that have no steps.
    static void addProfileRows() {
        QTest::addColumn<QString>("filePath");
        QTest::addColumn<QString>("fileName");

        QDir dir(kProfilesDir);
        for (const QString& f : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
            Profile p = loadProfile(dir.absoluteFilePath(f));
            if (p.steps().isEmpty()) continue;
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f) << f;
        }
    }

private slots:

    // ==========================================
    // Verify profiles directory exists and has files
    // ==========================================

    void profilesDirExists() {
        QDir dir(kProfilesDir);
        QVERIFY2(dir.exists(), qPrintable("Profiles dir not found: " + kProfilesDir));
        QStringList files = dir.entryList({"*.json"}, QDir::Files);
        QVERIFY2(files.size() >= 50,
                 qPrintable(QString("Expected 50+ profiles, found %1").arg(files.size())));
    }

    // ==========================================
    // Header: 5 bytes, correct structure
    // ==========================================

    void headerFormat_data() { addProfileRows(); }

    void headerFormat() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QByteArray header = p.toHeaderBytes();

        // Header must be exactly 5 bytes
        QCOMPARE(header.size(), 5);

        // Byte 0: HeaderV = 1
        QCOMPARE(uint8_t(header[0]), uint8_t(1));

        // Byte 1: NumberOfFrames = steps.size()
        QCOMPARE(uint8_t(header[1]), uint8_t(p.steps().size()));

        // Byte 2: NumberOfPreinfuseFrames
        QCOMPARE(uint8_t(header[2]), uint8_t(p.preinfuseFrameCount()));

        // Byte 3: MinimumPressure = 0 (de1app default, IgnoreLimit)
        QCOMPARE(uint8_t(header[3]), uint8_t(0));

        // Byte 4: MaximumFlow = encodeU8P4(6.0) = 96
        QCOMPARE(uint8_t(header[4]), BinaryCodec::encodeU8P4(6.0));
    }

    // ==========================================
    // Frame bytes: correct count, 8-byte frames
    // ==========================================

    void frameCount_data() { addProfileRows(); }

    void frameCount() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QList<QByteArray> frames = p.toFrameBytes();

        // Count how many extension frames we expect
        int extensionCount = 0;
        for (const ProfileFrame& step : p.steps()) {
            if (step.maxFlowOrPressure > 0)
                extensionCount++;
        }

        // Total = regular frames + extension frames + 1 tail frame
        qsizetype expectedTotal = p.steps().size() + extensionCount + 1;
        QCOMPARE(frames.size(), expectedTotal);

        // Every frame must be exactly 8 bytes
        for (qsizetype i = 0; i < frames.size(); ++i) {
            QVERIFY2(frames[i].size() == 8,
                     qPrintable(QString("Frame %1 is %2 bytes (expected 8) in %3")
                                .arg(i).arg(frames[i].size()).arg(fileName)));
        }
    }

    // ==========================================
    // Regular frame: index, flags, encode→decode
    // ==========================================

    void regularFrameRoundTrip_data() { addProfileRows(); }

    void regularFrameRoundTrip() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QList<QByteArray> allFrames = p.toFrameBytes();

        for (qsizetype i = 0; i < p.steps().size(); ++i) {
            const QByteArray& frame = allFrames[i];
            const ProfileFrame& step = p.steps()[i];

            // Byte 0: FrameToWrite = frame index
            QVERIFY2(uint8_t(frame[0]) == uint8_t(i),
                     qPrintable(QString("Frame %1 FrameToWrite=%2 in %3")
                                .arg(i).arg(uint8_t(frame[0])).arg(fileName)));

            // Byte 1: Flags match computeFlags()
            QVERIFY2(uint8_t(frame[1]) == step.computeFlags(),
                     qPrintable(QString("Frame %1 flags=%2 expected=%3 in %4")
                                .arg(i).arg(uint8_t(frame[1])).arg(step.computeFlags()).arg(fileName)));

            // Byte 2: SetVal (U8P4) decodes back within 1/16 of original
            double decodedSetVal = BinaryCodec::decodeU8P4(uint8_t(frame[2]));
            double originalSetVal = step.getSetVal();
            QVERIFY2(qAbs(decodedSetVal - originalSetVal) <= 0.0625,
                     qPrintable(QString("Frame %1 SetVal: decoded=%2 original=%3 in %4")
                                .arg(i).arg(decodedSetVal).arg(originalSetVal).arg(fileName)));

            // Byte 3: Temp (U8P1) decodes back within 0.5°C of original
            double decodedTemp = BinaryCodec::decodeU8P1(uint8_t(frame[3]));
            QVERIFY2(qAbs(decodedTemp - step.temperature) <= 0.5,
                     qPrintable(QString("Frame %1 Temp: decoded=%2 original=%3 in %4")
                                .arg(i).arg(decodedTemp).arg(step.temperature).arg(fileName)));

            // Byte 4: FrameLen (F8_1_7) decodes back within tolerance
            double decodedLen = BinaryCodec::decodeF8_1_7(uint8_t(frame[4]));
            // Short mode (<12.75): 0.1s precision. Long mode: 1s precision.
            double tolerance = (step.seconds < 12.75) ? 0.05 : 0.5;
            QVERIFY2(qAbs(decodedLen - step.seconds) <= tolerance,
                     qPrintable(QString("Frame %1 FrameLen: decoded=%2 original=%3 in %4")
                                .arg(i).arg(decodedLen).arg(step.seconds).arg(fileName)));

            // Byte 5: TriggerVal (U8P4) decodes back within 1/16
            double decodedTrigger = BinaryCodec::decodeU8P4(uint8_t(frame[5]));
            double originalTrigger = step.getTriggerVal();
            QVERIFY2(qAbs(decodedTrigger - originalTrigger) <= 0.0625,
                     qPrintable(QString("Frame %1 TriggerVal: decoded=%2 original=%3 in %4")
                                .arg(i).arg(decodedTrigger).arg(originalTrigger).arg(fileName)));

            // Bytes 6-7: MaxVol (U10P0) decodes back to original
            uint16_t maxVolEncoded = (uint8_t(frame[6]) << 8) | uint8_t(frame[7]);
            double decodedVol = BinaryCodec::decodeU10P0(maxVolEncoded);
            QVERIFY2(qAbs(decodedVol - step.volume) <= 1.0,
                     qPrintable(QString("Frame %1 MaxVol: decoded=%2 original=%3 in %4")
                                .arg(i).arg(decodedVol).arg(step.volume).arg(fileName)));
        }
    }

    // ==========================================
    // Flag bits: verify individual flag semantics
    // ==========================================

    void flagBitsCorrect_data() { addProfileRows(); }

    void flagBitsCorrect() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QList<QByteArray> allFrames = p.toFrameBytes();

        for (qsizetype i = 0; i < p.steps().size(); ++i) {
            uint8_t flags = uint8_t(allFrames[i][1]);
            const ProfileFrame& step = p.steps()[i];

            // IgnoreLimit (0x40) must always be set (de1app default)
            QVERIFY2(flags & DE1::FrameFlag::IgnoreLimit,
                     qPrintable(QString("Frame %1 missing IgnoreLimit in %2")
                                .arg(i).arg(fileName)));

            // CtrlF matches pump mode
            bool hasCtrlF = flags & DE1::FrameFlag::CtrlF;
            QVERIFY2(hasCtrlF == (step.pump == "flow"),
                     qPrintable(QString("Frame %1 CtrlF=%2 but pump='%3' in %4")
                                .arg(i).arg(hasCtrlF).arg(step.pump).arg(fileName)));

            // TMixTemp matches sensor
            bool hasTMixTemp = flags & DE1::FrameFlag::TMixTemp;
            QVERIFY2(hasTMixTemp == (step.sensor == "water"),
                     qPrintable(QString("Frame %1 TMixTemp=%2 but sensor='%3' in %4")
                                .arg(i).arg(hasTMixTemp).arg(step.sensor).arg(fileName)));

            // Interpolate matches transition
            bool hasInterpolate = flags & DE1::FrameFlag::Interpolate;
            QVERIFY2(hasInterpolate == (step.transition == "smooth"),
                     qPrintable(QString("Frame %1 Interpolate=%2 but transition='%3' in %4")
                                .arg(i).arg(hasInterpolate).arg(step.transition).arg(fileName)));

            // DoCompare matches exitIf (with valid exitType)
            bool hasDoCompare = flags & DE1::FrameFlag::DoCompare;
            bool expectDoCompare = step.exitIf &&
                (step.exitType == "pressure_over" || step.exitType == "pressure_under" ||
                 step.exitType == "flow_over" || step.exitType == "flow_under");
            QVERIFY2(hasDoCompare == expectDoCompare,
                     qPrintable(QString("Frame %1 DoCompare=%2 but exitIf=%3 exitType='%4' in %5")
                                .arg(i).arg(hasDoCompare).arg(step.exitIf).arg(step.exitType).arg(fileName)));
        }
    }

    // ==========================================
    // Extension frames: present when limiter > 0
    // ==========================================

    void extensionFrames_data() { addProfileRows(); }

    void extensionFrames() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QList<QByteArray> allFrames = p.toFrameBytes();

        // Collect extension frames (FrameToWrite >= 32)
        QMap<int, QByteArray> extensions;  // stepIndex → extension frame
        for (const QByteArray& frame : allFrames) {
            uint8_t ftwByte = uint8_t(frame[0]);
            if (ftwByte >= 32 && ftwByte < 64) {
                extensions[ftwByte - 32] = frame;
            }
        }

        // Verify each step with limiter > 0 has a corresponding extension
        for (qsizetype i = 0; i < p.steps().size(); ++i) {
            const ProfileFrame& step = p.steps()[i];
            if (step.maxFlowOrPressure > 0) {
                QVERIFY2(extensions.contains(int(i)),
                         qPrintable(QString("Frame %1 has limiter=%2 but no extension in %3")
                                    .arg(i).arg(step.maxFlowOrPressure).arg(fileName)));

                const QByteArray& ext = extensions[int(i)];
                // Byte 1: MaxFlowOrPressure (U8P4) round-trips
                double decodedLimiter = BinaryCodec::decodeU8P4(uint8_t(ext[1]));
                QVERIFY2(qAbs(decodedLimiter - step.maxFlowOrPressure) <= 0.0625,
                         qPrintable(QString("Frame %1 ext limiter: decoded=%2 original=%3 in %4")
                                    .arg(i).arg(decodedLimiter).arg(step.maxFlowOrPressure).arg(fileName)));

                // Byte 2: Range (U8P4) round-trips
                double decodedRange = BinaryCodec::decodeU8P4(uint8_t(ext[2]));
                QVERIFY2(qAbs(decodedRange - step.maxFlowOrPressureRange) <= 0.0625,
                         qPrintable(QString("Frame %1 ext range: decoded=%2 original=%3 in %4")
                                    .arg(i).arg(decodedRange).arg(step.maxFlowOrPressureRange).arg(fileName)));
            } else {
                QVERIFY2(!extensions.contains(int(i)),
                         qPrintable(QString("Frame %1 has limiter=0 but extension present in %2")
                                    .arg(i).arg(fileName)));
            }
        }
    }

    // ==========================================
    // Tail frame: correct position and format
    // ==========================================

    void tailFrame_data() { addProfileRows(); }

    void tailFrame() {
        QFETCH(QString, filePath);
        QFETCH(QString, fileName);

        Profile p = loadProfile(filePath);

        QList<QByteArray> allFrames = p.toFrameBytes();

        // Tail is the last frame
        const QByteArray& tail = allFrames.last();

        // Tail must be 8 bytes
        QCOMPARE(tail.size(), 8);

        // Byte 0: FrameToWrite = steps.size()
        QVERIFY2(uint8_t(tail[0]) == uint8_t(p.steps().size()),
                 qPrintable(QString("Tail FrameToWrite=%1 expected=%2 in %3")
                            .arg(uint8_t(tail[0])).arg(p.steps().size()).arg(fileName)));

        // Bytes 1-2: MaxTotalVol = U10P0(0) = 0x0400
        uint16_t tailVol = (uint8_t(tail[1]) << 8) | uint8_t(tail[2]);
        QCOMPARE(tailVol, BinaryCodec::encodeU10P0(0));

        // Bytes 3-7: padding (all zeros)
        for (int b = 3; b < 8; ++b) {
            QVERIFY2(tail[b] == 0,
                     qPrintable(QString("Tail byte %1 = %2 (expected 0) in %3")
                                .arg(b).arg(uint8_t(tail[b])).arg(fileName)));
        }
    }

    // ==========================================
    // Specific profile smoke tests
    // ==========================================

    void dFlowDefaultStructure() {
        // D-Flow default: 3 regular frames + extensions + tail
        QString path = kProfilesDir + "/d_flow_default.json";
        Profile p = loadProfile(path);
        if (p.steps().isEmpty()) QSKIP("d_flow_default.json not found or empty");

        QVERIFY(p.steps().size() >= 3);

        QList<QByteArray> frames = p.toFrameBytes();
        // Must have at least steps + tail
        QVERIFY(frames.size() >= p.steps().size() + 1);

        // Header sanity
        QByteArray header = p.toHeaderBytes();
        QCOMPARE(header.size(), 5);
        QCOMPARE(uint8_t(header[0]), uint8_t(1));
    }

    void bloomingEspressoHasExitConditions() {
        // Blooming espresso has multiple frames with exit conditions
        QString path = kProfilesDir + "/blooming_espresso.json";
        Profile p = loadProfile(path);
        if (p.steps().isEmpty()) QSKIP("blooming_espresso.json not found or empty");

        QList<QByteArray> frames = p.toFrameBytes();

        // At least one frame should have DoCompare set
        bool foundDoCompare = false;
        for (qsizetype i = 0; i < p.steps().size(); ++i) {
            uint8_t flags = uint8_t(frames[i][1]);
            if (flags & DE1::FrameFlag::DoCompare) {
                foundDoCompare = true;
                break;
            }
        }
        QVERIFY2(foundDoCompare,
                 "Blooming espresso should have at least one frame with DoCompare");
    }

    void simpleProfileFrameGeneration() {
        // Simple pressure profiles (settings_2a) should generate frames
        // and encode without errors
        QString path = kProfilesDir + "/default.json";
        Profile p = loadProfile(path);
        if (p.steps().isEmpty()) QSKIP("default.json not found or empty");

        QByteArray header = p.toHeaderBytes();
        QList<QByteArray> frames = p.toFrameBytes();

        // Basic sanity
        QCOMPARE(header.size(), 5);
        QVERIFY(frames.size() >= 2);  // At least 1 frame + tail

        // All frames 8 bytes
        for (const QByteArray& f : frames)
            QCOMPARE(f.size(), 8);
    }

    void profileWithLimiterHasExtension() {
        // Verify every profile with a limiter has extension frames
        QDir dir(kProfilesDir);
        int checked = 0;
        for (const QString& f : dir.entryList({"*.json"}, QDir::Files)) {
            Profile p = loadProfile(dir.absoluteFilePath(f));
            bool hasLimiter = false;
            for (const ProfileFrame& step : p.steps()) {
                if (step.maxFlowOrPressure > 0) {
                    hasLimiter = true;
                    break;
                }
            }
            if (!hasLimiter) continue;

            QList<QByteArray> frames = p.toFrameBytes();
            QVERIFY2(frames.size() > p.steps().size() + 1,
                     qPrintable(QString("Profile '%1' has limiter but no extension frames").arg(f)));
            checked++;
        }
        QVERIFY2(checked > 0, "No profiles with limiters found");
    }

    void volumeEncodingU10P0() {
        // Verify U10P0 flag bit is always set in MaxVol bytes for all profiles
        QDir dir(kProfilesDir);
        int checked = 0;
        for (const QString& f : dir.entryList({"*.json"}, QDir::Files)) {
            Profile p = loadProfile(dir.absoluteFilePath(f));
            if (p.steps().isEmpty()) continue;

            QList<QByteArray> frames = p.toFrameBytes();
            for (qsizetype i = 0; i < p.steps().size(); ++i) {
                uint16_t maxVol = (uint8_t(frames[i][6]) << 8) | uint8_t(frames[i][7]);
                QVERIFY2(maxVol & 0x0400,
                         qPrintable(QString("Frame %1 in %2: U10P0 flag bit not set (0x%3)")
                                    .arg(i).arg(f).arg(maxVol, 4, 16, QChar('0'))));
            }
            checked++;
        }
        QVERIFY2(checked >= 50, qPrintable(QString("Only %1 profiles checked").arg(checked)));
    }

    void allProfilesEncodeWithoutCrash() {
        // Smoke test: every profile encodes header + frames without crashing
        QDir dir(kProfilesDir);
        int count = 0;
        for (const QString& f : dir.entryList({"*.json"}, QDir::Files)) {
            Profile p = loadProfile(dir.absoluteFilePath(f));
            if (p.steps().isEmpty()) continue;

            QByteArray header = p.toHeaderBytes();
            QCOMPARE(header.size(), 5);

            QList<QByteArray> frames = p.toFrameBytes();
            QVERIFY(frames.size() >= 2);  // At least 1 frame + tail
            count++;
        }
        QVERIFY2(count >= 50, qPrintable(QString("Only %1 profiles encoded").arg(count)));
    }
};

QTEST_GUILESS_MAIN(tst_BleFidelity)
#include "tst_blefidelity.moc"
