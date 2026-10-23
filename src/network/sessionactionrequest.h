#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include "steameditrequest.h"

namespace SessionActionRequest {

enum class Action {
    Tare,
    Scan,
    Espresso,
    Steam,
    Hotwater,
    Flush,
    MilkStart,
    MilkCancel,
    MilkLock,
    MilkUnlock,
    PortalReconnect,
    SelectPitcher,
    SteamPrepare,
    SteamSelectPitcher,
    SteamAdjustTime,
    SteamStop,
    SteamBack,
    SteamEditor,
    SteamGraphTrace,
    UiPreference
};

struct Result {
    bool valid = false;
    Action action = Action::Tare;
    QString actionName;
    QString requestId;
    QString requestIdKey;
    int index = -1;
    int delta = 0;
    bool expectedSoftStopped = false;
    QString graphTrace;
    bool graphTraceEnabled = false;
    bool graphTraceExpectedEnabled = false;
    QString uiPreference;
    QJsonValue uiPreferenceValue;
    QJsonValue uiPreferenceExpectedValue;
    QString steamPitchersRevision;
    QString controlContextId;
    QString expectedRevision;
    SteamEditRequest::Result steamEditor;
    int httpStatus = 400;
    QString error;
};

constexpr qsizetype MaximumBodyBytes = 4096;

bool isPotentialTarget(const QString& target);
QString endpointPath(Action action);
QString actionName(Action action);
bool actionForName(const QString& name, Action* action);
bool isPreparationAction(Action action);
bool isSteamPageAction(Action action);
Result validate(const QString& method, const QString& target, const QByteArray& body,
                const QString& expectedPackageId, const QString& expectedHostId);

} // namespace SessionActionRequest
