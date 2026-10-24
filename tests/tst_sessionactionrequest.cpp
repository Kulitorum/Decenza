#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "network/sessionactionrequest.h"

namespace {
const QString kPackageId = QStringLiteral("package-17");
const QString kHostId = QStringLiteral("host-abc");
const QString kRequestId = QStringLiteral("123e4567-e89b-12d3-a456-426614174000");
const QString kControlContextId = QStringLiteral("f7f63b52-2f01-4d1f-aea9-9f53660cf1b3");

QJsonObject validObject(const QString& packageId = kPackageId,
                        const QString& hostId = kHostId,
                        const QString& requestId = kRequestId)
{
    return QJsonObject{
        {QStringLiteral("packageId"), packageId},
        {QStringLiteral("hostId"), hostId},
        {QStringLiteral("requestId"), requestId}
    };
}

QByteArray body(const QJsonObject& object = validObject())
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject validSelectionObject(int index = 0,
                                 const QString& revision = QString(64, QLatin1Char('a')))
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("steamPitchersRevision"), revision);
    return object;
}

QJsonObject validSteamPagePitcherObject(int index = 0,
                                        const QString& revision = QString(64, QLatin1Char('a')))
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("steamPitchersRevision"), revision);
    object.insert(QStringLiteral("controlContextId"), kControlContextId);
    return object;
}

QJsonObject validSteamPageAdjustObject(int delta = 5)
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("delta"), delta);
    object.insert(QStringLiteral("controlContextId"), kControlContextId);
    return object;
}

QJsonObject validSteamStopObject(bool expectedSoftStopped = false)
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("controlContextId"), kControlContextId);
    object.insert(QStringLiteral("expectedSoftStopped"), expectedSoftStopped);
    return object;
}

QJsonObject validSteamBackObject()
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("controlContextId"), kControlContextId);
    return object;
}

SessionActionRequest::Result validate(const QString& method, const QString& target,
                                      const QByteArray& payload,
                                      const QString& packageId = kPackageId,
                                      const QString& hostId = kHostId)
{
    return SessionActionRequest::validate(method, target, payload, packageId, hostId);
}
} // namespace

class tst_SessionActionRequest : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void acceptsEachExactActionRouteAndNormalizesUuidKey()
    {
        const QString uppercaseId = kRequestId.toUpper();
        const auto tare = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), body(validObject(kPackageId, kHostId, uppercaseId)));
        QVERIFY2(tare.valid, qPrintable(tare.error));
        QCOMPARE(static_cast<int>(tare.action),
                 static_cast<int>(SessionActionRequest::Action::Tare));
        QCOMPARE(tare.actionName, QStringLiteral("tare"));
        QCOMPARE(tare.requestId, uppercaseId);
        QCOMPARE(tare.requestIdKey, kRequestId);

        const auto scan = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/scan"), body());
        QVERIFY2(scan.valid, qPrintable(scan.error));
        QCOMPARE(static_cast<int>(scan.action),
                 static_cast<int>(SessionActionRequest::Action::Scan));
        QCOMPARE(scan.actionName, QStringLiteral("scan"));
        QCOMPARE(scan.requestId, kRequestId);
        QCOMPARE(scan.requestIdKey, kRequestId);

        const auto portal = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/portal/reconnect"), body());
        QVERIFY2(portal.valid, qPrintable(portal.error));
        QCOMPARE(static_cast<int>(portal.action),
                 static_cast<int>(SessionActionRequest::Action::PortalReconnect));
        QCOMPARE(portal.actionName, QStringLiteral("portal-reconnect"));
        QVERIFY(SessionActionRequest::isPotentialTarget(
            QStringLiteral("/api/session/portal/reconnect")));
        QVERIFY(!SessionActionRequest::isPreparationAction(portal.action));
    }

    void acceptsPreparationRoutesFromTheSharedActionTable()
    {
        const QString actions[] = {
            QStringLiteral("espresso"), QStringLiteral("steam"),
            QStringLiteral("hotwater"), QStringLiteral("flush"),
            QStringLiteral("milk-start"), QStringLiteral("milk-cancel"),
            QStringLiteral("milk-lock"), QStringLiteral("milk-unlock")
        };
        for (const QString& name : actions) {
            SessionActionRequest::Action action;
            QVERIFY2(SessionActionRequest::actionForName(name, &action), qPrintable(name));
            QVERIFY(SessionActionRequest::isPreparationAction(action));
            QCOMPARE(SessionActionRequest::actionName(action), name);
            const QString path = SessionActionRequest::endpointPath(action);
            QVERIFY(!path.isEmpty());
            const auto parsed = validate(QStringLiteral("POST"), path, body());
            QVERIFY2(parsed.valid, qPrintable(parsed.error));
            QCOMPARE(static_cast<int>(parsed.action), static_cast<int>(action));
            QCOMPARE(parsed.actionName, name);
            QVERIFY(SessionActionRequest::isPotentialTarget(path));

            const auto wrongMethod = validate(QStringLiteral("GET"), path, body());
            QVERIFY(!wrongMethod.valid);
            QCOMPARE(wrongMethod.httpStatus, 405);
            const auto withQuery = validate(QStringLiteral("POST"), path + QStringLiteral("?x=1"), body());
            QVERIFY(!withQuery.valid);
            QCOMPARE(withQuery.httpStatus, 400);
        }
        QVERIFY(!SessionActionRequest::isPreparationAction(SessionActionRequest::Action::Tare));
        QVERIFY(!SessionActionRequest::isPreparationAction(SessionActionRequest::Action::Scan));
        SessionActionRequest::Action unused;
        QVERIFY(!SessionActionRequest::actionForName(QStringLiteral("brew"), &unused));
    }

    void acceptsGuardedPitcherSelectionWithStrictTypedFields()
    {
        const QString path = QStringLiteral("/api/session/steam/select-pitcher");
        const auto parsed = validate(QStringLiteral("POST"), path, body(validSelectionObject(-1)));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(static_cast<int>(parsed.action),
                 static_cast<int>(SessionActionRequest::Action::SelectPitcher));
        QCOMPARE(parsed.actionName, QStringLiteral("select-pitcher"));
        QCOMPARE(parsed.index, -1);
        QCOMPARE(parsed.steamPitchersRevision, QString(64, QLatin1Char('a')));
        QVERIFY(SessionActionRequest::isPotentialTarget(path));
        SessionActionRequest::Action action;
        QVERIFY(SessionActionRequest::actionForName(QStringLiteral("select-pitcher"), &action));
        QCOMPARE(SessionActionRequest::endpointPath(action), path);
    }

    void rejectsMalformedPitcherSelectionAndDoesNotRelaxOtherActions()
    {
        const QString path = QStringLiteral("/api/session/steam/select-pitcher");
        const QJsonValue invalidIndexes[] = {
            QJsonValue(1.5), QJsonValue(true), QJsonValue(QJsonValue::Null),
            QJsonValue(2147483648.0), QJsonValue(-2147483649.0), QJsonValue(1.0e100)
        };
        for (const QJsonValue& index : invalidIndexes) {
            QJsonObject invalid = validSelectionObject();
            invalid.insert(QStringLiteral("index"), index);
            const auto result = validate(QStringLiteral("POST"), path, body(invalid));
            QVERIFY(!result.valid);
            QCOMPARE(result.httpStatus, 400);
        }

        QJsonObject object = validSelectionObject();
        object.insert(QStringLiteral("index"), QStringLiteral("0"));
        auto result = validate(QStringLiteral("POST"), path, body(object));
        QVERIFY(!result.valid);

        object = validSelectionObject();
        object.insert(QStringLiteral("steamPitchersRevision"), QString(64, QLatin1Char('A')));
        result = validate(QStringLiteral("POST"), path, body(object));
        QVERIFY(!result.valid);

        object = validSelectionObject();
        object.insert(QStringLiteral("milkFallbackG"), 250.0);
        result = validate(QStringLiteral("POST"), path, body(object));
        QVERIFY(!result.valid);

        result = validate(QStringLiteral("GET"), path, body(validSelectionObject()));
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 405);
        result = validate(QStringLiteral("POST"), path + QStringLiteral("?x=1"),
                          body(validSelectionObject()));
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 400);
    }

    void acceptsSteamPageControlRoutesWithTheirExactPayloads()
    {
        const struct {
            const char* path;
            SessionActionRequest::Action action;
            const char* name;
            QJsonObject (*payload)();
        } cases[] = {
            {"/api/session/steam/prepare", SessionActionRequest::Action::SteamPrepare,
             "steam-prepare", [] { return validSteamPagePitcherObject(-1); }},
            {"/api/session/steam/select-page-pitcher", SessionActionRequest::Action::SteamSelectPitcher,
             "steam-select-pitcher", [] { return validSteamPagePitcherObject(1); }},
            {"/api/session/steam/adjust-time", SessionActionRequest::Action::SteamAdjustTime,
             "steam-adjust-time", [] { return validSteamPageAdjustObject(-5); }}
        };

        for (const auto& test : cases) {
            const QString path = QString::fromLatin1(test.path);
            const auto parsed = validate(QStringLiteral("POST"), path, body(test.payload()));
            QVERIFY2(parsed.valid, qPrintable(parsed.error));
            QCOMPARE(static_cast<int>(parsed.action), static_cast<int>(test.action));
            QCOMPARE(parsed.actionName, QString::fromLatin1(test.name));
            QVERIFY(SessionActionRequest::isSteamPageAction(parsed.action));
            QCOMPARE(SessionActionRequest::endpointPath(parsed.action), path);
            QVERIFY(SessionActionRequest::isPotentialTarget(path));
        }
        QVERIFY(!SessionActionRequest::isSteamPageAction(SessionActionRequest::Action::SelectPitcher));
    }

    void rejectsStaleContextAndMalformedSteamPagePayloads()
    {
        const QString preparePath = QStringLiteral("/api/session/steam/prepare");
        QJsonObject object = validSteamPagePitcherObject(0);
        object.remove(QStringLiteral("controlContextId"));
        auto result = validate(QStringLiteral("POST"), preparePath, body(object));
        QVERIFY(!result.valid);

        object = validSteamPagePitcherObject(0);
        object.insert(QStringLiteral("controlContextId"), QStringLiteral("{f7f63b52-2f01-4d1f-aea9-9f53660cf1b3}"));
        result = validate(QStringLiteral("POST"), preparePath, body(object));
        QVERIFY(!result.valid);

        object = validSteamPagePitcherObject(0);
        object.insert(QStringLiteral("controlContextId"), kControlContextId.toUpper());
        result = validate(QStringLiteral("POST"), preparePath, body(object));
        QVERIFY(!result.valid);

        object = validSteamPagePitcherObject(0);
        object.insert(QStringLiteral("milkFallbackG"), 250.0);
        result = validate(QStringLiteral("POST"), preparePath, body(object));
        QVERIFY(!result.valid);

        const QString selectPath = QStringLiteral("/api/session/steam/select-page-pitcher");
        object = validSteamPagePitcherObject();
        object.insert(QStringLiteral("index"), true);
        result = validate(QStringLiteral("POST"), selectPath, body(object));
        QVERIFY(!result.valid);

        const QString adjustPath = QStringLiteral("/api/session/steam/adjust-time");
        for (const QJsonValue& delta : {QJsonValue(4), QJsonValue(5.5), QJsonValue(true),
                                        QJsonValue(QJsonValue::Null), QJsonValue(10)}) {
            object = validSteamPageAdjustObject();
            object.insert(QStringLiteral("delta"), delta);
            result = validate(QStringLiteral("POST"), adjustPath, body(object));
            QVERIFY(!result.valid);
        }

        result = validate(QStringLiteral("GET"), adjustPath, body(validSteamPageAdjustObject()));
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 405);
        result = validate(QStringLiteral("POST"), adjustPath + QStringLiteral("?x=1"),
                          body(validSteamPageAdjustObject()));
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 400);
    }

    void acceptsSteamStopAndBackWithExactGuards()
    {
        const struct {
            const char* path;
            SessionActionRequest::Action action;
            const char* name;
            QJsonObject (*payload)();
        } cases[] = {
            {"/api/session/steam/stop", SessionActionRequest::Action::SteamStop,
             "steam-stop", [] { return validSteamStopObject(); }},
            {"/api/session/steam/back", SessionActionRequest::Action::SteamBack,
             "steam-back", [] { return validSteamBackObject(); }}
        };
        for (const auto& test : cases) {
            const QString path = QString::fromLatin1(test.path);
            const auto parsed = validate(QStringLiteral("POST"), path, body(test.payload()));
            QVERIFY2(parsed.valid, qPrintable(parsed.error));
            QCOMPARE(static_cast<int>(parsed.action), static_cast<int>(test.action));
            QCOMPARE(parsed.actionName, QString::fromLatin1(test.name));
            QVERIFY(SessionActionRequest::isSteamPageAction(parsed.action));
            QCOMPARE(SessionActionRequest::endpointPath(parsed.action), path);
            QVERIFY(SessionActionRequest::isPotentialTarget(path));
        }

        const auto softStop = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/steam/stop"), body(validSteamStopObject(true)));
        QVERIFY2(softStop.valid, qPrintable(softStop.error));
        QVERIFY(softStop.expectedSoftStopped);
    }

    void acceptsSteamGraphTraceWithExpectedValueGuard()
    {
        const QString path = QStringLiteral("/api/session/steam/graph-trace");
        QJsonObject object = validObject();
        object.insert(QStringLiteral("trace"), QStringLiteral("pressure"));
        object.insert(QStringLiteral("enabled"), false);
        object.insert(QStringLiteral("expectedEnabled"), true);
        const auto parsed = validate(QStringLiteral("POST"), path, body(object));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(static_cast<int>(parsed.action),
                 static_cast<int>(SessionActionRequest::Action::SteamGraphTrace));
        QCOMPARE(parsed.actionName, QStringLiteral("steam-graph-trace"));
        QCOMPARE(parsed.graphTrace, QStringLiteral("pressure"));
        QVERIFY(!parsed.graphTraceEnabled);
        QVERIFY(parsed.graphTraceExpectedEnabled);

        for (const QString& trace : {QStringLiteral("flow"), QStringLiteral("temperature")}) {
            object.insert(QStringLiteral("trace"), trace);
            QVERIFY2(validate(QStringLiteral("POST"), path, body(object)).valid,
                     qPrintable(trace));
        }
        object.insert(QStringLiteral("trace"), QStringLiteral("showPressure"));
        QCOMPARE(validate(QStringLiteral("POST"), path, body(object)).httpStatus, 400);
        object.insert(QStringLiteral("trace"), QStringLiteral("flow"));
        object.insert(QStringLiteral("enabled"), QStringLiteral("false"));
        QCOMPARE(validate(QStringLiteral("POST"), path, body(object)).httpStatus, 400);
        object = validObject();
        object.insert(QStringLiteral("trace"), QStringLiteral("flow"));
        object.insert(QStringLiteral("enabled"), false);
        object.insert(QStringLiteral("expectedEnabled"), true);
        object.insert(QStringLiteral("extra"), true);
        QCOMPARE(validate(QStringLiteral("POST"), path, body(object)).httpStatus, 400);
    }

    void rejectsMalformedStopBackPayloadsAndQueries()
    {
        const QString stopPath = QStringLiteral("/api/session/steam/stop");
        QJsonObject object = validSteamStopObject();
        object.remove(QStringLiteral("expectedSoftStopped"));
        QVERIFY(!validate(QStringLiteral("POST"), stopPath, body(object)).valid);
        for (const QJsonValue& value : {QJsonValue(0), QJsonValue(1), QJsonValue(QStringLiteral("false")),
                                        QJsonValue(QJsonValue::Null)}) {
            object = validSteamStopObject();
            object.insert(QStringLiteral("expectedSoftStopped"), value);
            QVERIFY(!validate(QStringLiteral("POST"), stopPath, body(object)).valid);
        }
        object = validSteamStopObject();
        object.insert(QStringLiteral("unexpected"), true);
        QVERIFY(!validate(QStringLiteral("POST"), stopPath, body(object)).valid);
        QCOMPARE(validate(QStringLiteral("GET"), stopPath, body(validSteamStopObject())).httpStatus, 405);
        QCOMPARE(validate(QStringLiteral("POST"), stopPath + QStringLiteral("?x=1"),
                         body(validSteamStopObject())).httpStatus, 400);

        const QString backPath = QStringLiteral("/api/session/steam/back");
        object = validSteamBackObject();
        object.remove(QStringLiteral("controlContextId"));
        QVERIFY(!validate(QStringLiteral("POST"), backPath, body(object)).valid);
        object = validSteamBackObject();
        object.insert(QStringLiteral("expectedSoftStopped"), false);
        QVERIFY(!validate(QStringLiteral("POST"), backPath, body(object)).valid);
        QCOMPARE(validate(QStringLiteral("GET"), backPath, body(validSteamBackObject())).httpStatus, 405);
    }

    void rejectsWrongMethodTargetAndQuery()
    {
        auto result = validate(QStringLiteral("GET"),
            QStringLiteral("/api/session/scale/tare"), body());
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 405);

        result = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare?force=true"), body());
        QVERIFY(!result.valid);
        QCOMPARE(result.httpStatus, 400);

        result = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/unknown"), body());
        QVERIFY(!result.valid);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(SessionActionRequest::isPotentialTarget(
            QStringLiteral("/api/session/scale/tare")));
        QVERIFY(SessionActionRequest::isPotentialTarget(
            QStringLiteral("/api/session/scale/scan")));
        QVERIFY(SessionActionRequest::isPotentialTarget(
            QStringLiteral("/api/session/portal/reconnect")));
        QVERIFY(!SessionActionRequest::isPotentialTarget(QStringLiteral("/api/state")));
    }

    void requiresExactIdentityAndThreeStringKeys()
    {
        auto result = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), body(validObject(QStringLiteral("other"))));
        QVERIFY(!result.valid);

        result = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), body(validObject(kPackageId, QStringLiteral("other"))));
        QVERIFY(!result.valid);

        for (const QString& key : {QStringLiteral("packageId"), QStringLiteral("hostId"),
                                   QStringLiteral("requestId")}) {
            QJsonObject missing = validObject();
            missing.remove(key);
            result = validate(QStringLiteral("POST"),
                QStringLiteral("/api/session/scale/tare"), body(missing));
            QVERIFY2(!result.valid, qPrintable(key + QStringLiteral(" missing")));

            QJsonObject wrongType = validObject();
            wrongType.insert(key, 17);
            result = validate(QStringLiteral("POST"),
                QStringLiteral("/api/session/scale/tare"), body(wrongType));
            QVERIFY2(!result.valid, qPrintable(key + QStringLiteral(" type")));
        }

        QJsonObject extra = validObject();
        extra.insert(QStringLiteral("enabled"), true);
        result = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), body(extra));
        QVERIFY(!result.valid);
    }

    void acceptsOnlyCanonicalUnbracedUuid()
    {
        const QString invalidIds[] = {
            QString(),
            QStringLiteral("{123e4567-e89b-12d3-a456-426614174000}"),
            QStringLiteral("123e4567e89b12d3a456426614174000"),
            QStringLiteral("123e4567-e89b-12d3-a456-42661417400z")
        };
        for (const QString& requestId : invalidIds) {
            const auto result = validate(QStringLiteral("POST"),
                QStringLiteral("/api/session/scale/scan"), body(validObject(kPackageId, kHostId, requestId)));
            QVERIFY2(!result.valid, qPrintable(QStringLiteral("accepted requestId: ") + requestId));
        }
    }

    void uiPreferencesAreAllowlistedAndCompareAgainstTypedExpectedValues()
    {
        const QString path = QStringLiteral("/api/session/ui-preference");
        QJsonObject mode = validObject();
        mode.insert(QStringLiteral("preference"), QStringLiteral("steamViewMode"));
        mode.insert(QStringLiteral("value"), QStringLiteral("chart"));
        mode.insert(QStringLiteral("expectedValue"), QStringLiteral("timer"));
        auto parsed = validate(QStringLiteral("POST"), path, body(mode));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.actionName, QStringLiteral("ui-preference"));
        QCOMPARE(parsed.uiPreference, QStringLiteral("steamViewMode"));
        QCOMPARE(parsed.uiPreferenceValue.toString(), QStringLiteral("chart"));
        QCOMPARE(parsed.uiPreferenceExpectedValue.toString(), QStringLiteral("timer"));

        QJsonObject coach = validObject();
        coach.insert(QStringLiteral("preference"), QStringLiteral("steamCoachVisualEnabled"));
        coach.insert(QStringLiteral("value"), true);
        coach.insert(QStringLiteral("expectedValue"), false);
        parsed = validate(QStringLiteral("POST"), path, body(coach));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.uiPreferenceValue.toBool(), true);
        QCOMPARE(parsed.uiPreferenceExpectedValue.toBool(), false);

        for (const QString& invalid : {QStringLiteral("unknown"),
                                       QStringLiteral("extractionAnnouncementInterval")}) {
            QJsonObject object = coach;
            object.insert(QStringLiteral("preference"), invalid);
            QVERIFY(!validate(QStringLiteral("POST"), path, body(object)).valid);
        }
        coach.insert(QStringLiteral("value"), QStringLiteral("true"));
        QVERIFY(!validate(QStringLiteral("POST"), path, body(coach)).valid);

        QJsonObject advanced = validObject();
        advanced.insert(QStringLiteral("preference"), QStringLiteral("advancedMode"));
        advanced.insert(QStringLiteral("value"), true);
        advanced.insert(QStringLiteral("expectedValue"), false);
        parsed = validate(QStringLiteral("POST"), path, body(advanced));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.uiPreference, QStringLiteral("advancedMode"));
        QCOMPARE(parsed.uiPreferenceValue.toBool(), true);

        QJsonObject flow = validObject();
        flow.insert(QStringLiteral("preference"), QStringLiteral("flowMultiplier"));
        flow.insert(QStringLiteral("value"), 3);
        flow.insert(QStringLiteral("expectedValue"), 2);
        parsed = validate(QStringLiteral("POST"), path, body(flow));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.uiPreference, QStringLiteral("flowMultiplier"));
        QCOMPARE(parsed.uiPreferenceValue.toInt(), 3);
        flow.insert(QStringLiteral("value"), 1.5);
        QVERIFY(!validate(QStringLiteral("POST"), path, body(flow)).valid);
        flow.insert(QStringLiteral("value"), 4);
        QVERIFY(!validate(QStringLiteral("POST"), path, body(flow)).valid);
        flow.insert(QStringLiteral("value"), 1);
        flow.insert(QStringLiteral("expectedValue"), false);
        QVERIFY(!validate(QStringLiteral("POST"), path, body(flow)).valid);

        mode.insert(QStringLiteral("value"), QStringLiteral("native"));
        QVERIFY(!validate(QStringLiteral("POST"), path, body(mode)).valid);
        mode.insert(QStringLiteral("value"), QStringLiteral("chart"));
        mode.insert(QStringLiteral("extra"), true);
        QVERIFY(!validate(QStringLiteral("POST"), path, body(mode)).valid);
    }

    void enforcesFourKiBBodyLimitAtBoundary()
    {
        QByteArray payload = body();
        QVERIFY(payload.size() < SessionActionRequest::MaximumBodyBytes);
        payload.prepend(QByteArray(SessionActionRequest::MaximumBodyBytes - payload.size(), ' '));
        QCOMPARE(payload.size(), SessionActionRequest::MaximumBodyBytes);
        const auto atLimit = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), payload);
        QVERIFY2(atLimit.valid, qPrintable(atLimit.error));

        payload.prepend(' ');
        QCOMPARE(payload.size(), SessionActionRequest::MaximumBodyBytes + 1);
        const auto overLimit = validate(QStringLiteral("POST"),
            QStringLiteral("/api/session/scale/tare"), payload);
        QVERIFY(!overLimit.valid);
        QCOMPARE(overLimit.httpStatus, 413);
    }
};

QTEST_GUILESS_MAIN(tst_SessionActionRequest)
#include "tst_sessionactionrequest.moc"
