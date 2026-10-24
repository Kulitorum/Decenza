#include "../src/remote/calibrationoperationpolicy.h"

#include <QFile>
#include <QRegularExpression>
#include <QtTest>

class CalibrationOperationPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void operationAllowListRejectsGenericSetters()
    {
        QVERIFY(CalibrationOperationPolicy::supported(QStringLiteral("setHeaterValue")));
        QVERIFY(CalibrationOperationPolicy::supported(QStringLiteral("resetSawLearning")));
        QVERIFY(!CalibrationOperationPolicy::supported(QStringLiteral("setProperty")));
        QVERIFY(!CalibrationOperationPolicy::supported(QStringLiteral("writeMMR")));
    }

    void heaterRangesMatchTheExistingControls()
    {
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterIdleTemp"), 0));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterIdleTemp"), 990));
        QVERIFY(!CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterIdleTemp"), 991));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupFlow"), 5));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupFlow"), 60));
        QVERIFY(!CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupFlow"), 4));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterTestFlow"), 80));
        QVERIFY(!CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterTestFlow"), 81));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupTimeout"), 10));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupTimeout"), 300));
        QVERIFY(!CalibrationOperationPolicy::heaterValueValid(QStringLiteral("heaterWarmupTimeout"), 9));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("fanThreshold"), 0));
        QVERIFY(CalibrationOperationPolicy::heaterValueValid(QStringLiteral("fanThreshold"), 60));
        QVERIFY(!CalibrationOperationPolicy::heaterValueValid(QStringLiteral("other"), 1));
    }

    void onlyKnownHeaterVoltagesAreAccepted()
    {
        QVERIFY(CalibrationOperationPolicy::heaterVoltageValid(120));
        QVERIFY(CalibrationOperationPolicy::heaterVoltageValid(230));
        QVERIFY(!CalibrationOperationPolicy::heaterVoltageValid(115));
        QVERIFY(!CalibrationOperationPolicy::heaterVoltageValid(0));
    }

    void calibrationPagesSaveAgainstTheRevisionTheirDraftWasValidatedFrom()
    {
        QFile flowFile(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/FlowCalibrationPage.qml"));
        QVERIFY(flowFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString flowQml = QString::fromUtf8(flowFile.readAll());
        QVERIFY(QRegularExpression(
                    QStringLiteral("FlowCalibrationService\\.save\\(\\s*"
                                   "calibrationPage\\.multiplierDraft,\\s*"
                                   "calibrationPage\\.draftCalibrationRevision\\)"))
                    .match(flowQml).hasMatch());
        QVERIFY(flowQml.contains(QStringLiteral(
            "draftCalibrationRevision = FlowCalibrationService.calibrationRevision")));

        QFile sensorFile(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/SensorCalibrationPage.qml"));
        QVERIFY(sensorFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString sensorQml = QString::fromUtf8(sensorFile.readAll());
        QVERIFY(sensorQml.contains(QStringLiteral(
            "validatedCalibrationRevision === SensorCalibrationService.snapshot.calibrationRevision")));
        QVERIFY(sensorQml.contains(QStringLiteral(
            "validatedContextRevision === SensorCalibrationService.snapshot.contextRevision")));
        QVERIFY(QRegularExpression(
                    QStringLiteral("SensorCalibrationService\\.applyCorrection\\(\\s*"
                                   "calibrationPage\\.sensor,\\s*calibrationPage\\.entryValue,\\s*"
                                   "calibrationPage\\.validatedContextRevision,\\s*"
                                   "calibrationPage\\.validatedCalibrationRevision\\)"))
                    .match(sensorQml).hasMatch());
        QVERIFY(QRegularExpression(
                    QStringLiteral("if \\(snapshot\\.calibrationRevision !=="
                                   " calibrationPage\\._lastCalibrationRevision\\) \\{"
                                   "[\\s\\S]*?calibrationPage\\.entryTouched = false;?"
                                   "[\\s\\S]*?confirmDialog\\.close\\(\\);?"))
                    .match(sensorQml).hasMatch());
        QVERIFY(QRegularExpression(
                    QStringLiteral("enabled: calibrationPage\\.entryValid\\s*"
                                   "&& SensorCalibrationService\\.ready\\s*"
                                   "&& SensorCalibrationService\\.connected\\s*"
                                   "&& !SensorCalibrationService\\.busy"))
                    .match(sensorQml).hasMatch());
    }
};

QTEST_APPLESS_MAIN(CalibrationOperationPolicyTest)
#include "tst_calibrationoperationpolicy.moc"
