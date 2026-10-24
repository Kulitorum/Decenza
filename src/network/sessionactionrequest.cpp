#include "sessionactionrequest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace SessionActionRequest {
namespace {
struct Endpoint {
    const char* path;
    Action action;
    const char* name;
};

constexpr Endpoint kEndpoints[] = {
    {"/api/session/scale/tare", Action::Tare, "tare"},
    {"/api/session/scale/scan", Action::Scan, "scan"},
    {"/api/session/preparation/espresso", Action::Espresso, "espresso"},
    {"/api/session/preparation/steam", Action::Steam, "steam"},
    {"/api/session/preparation/hotwater", Action::Hotwater, "hotwater"},
    {"/api/session/preparation/flush", Action::Flush, "flush"},
    {"/api/session/preparation/milk-start", Action::MilkStart, "milk-start"},
    {"/api/session/preparation/milk-cancel", Action::MilkCancel, "milk-cancel"},
    {"/api/session/preparation/milk-lock", Action::MilkLock, "milk-lock"},
    {"/api/session/preparation/milk-unlock", Action::MilkUnlock, "milk-unlock"},
    {"/api/session/portal/reconnect", Action::PortalReconnect, "portal-reconnect"},
    {"/api/session/steam/select-pitcher", Action::SelectPitcher, "select-pitcher"},
    {"/api/session/steam/prepare", Action::SteamPrepare, "steam-prepare"},
    {"/api/session/steam/select-page-pitcher", Action::SteamSelectPitcher, "steam-select-pitcher"},
    {"/api/session/steam/adjust-time", Action::SteamAdjustTime, "steam-adjust-time"},
    {"/api/session/steam/stop", Action::SteamStop, "steam-stop"},
    {"/api/session/steam/back", Action::SteamBack, "steam-back"},
    {"/api/session/steam/editor", Action::SteamEditor, "steam-editor"},
    {"/api/session/steam/graph-trace", Action::SteamGraphTrace, "steam-graph-trace"},
    {"/api/session/ui-preference", Action::UiPreference, "ui-preference"}
};

const Endpoint* endpointForTarget(const QString& target, bool allowQuery)
{
    for (const Endpoint& endpoint : kEndpoints) {
        const QString path = QString::fromLatin1(endpoint.path);
        if (target == path || (allowQuery && target.startsWith(path + QLatin1Char('?'))))
            return &endpoint;
    }
    return nullptr;
}

Result failure(const Endpoint* endpoint, int status, const QString& error)
{
    Result result;
    if (endpoint) {
        result.action = endpoint->action;
        result.actionName = QString::fromLatin1(endpoint->name);
    }
    result.httpStatus = status;
    result.error = error;
    return result;
}
} // namespace

QString endpointPath(Action action)
{
    for (const Endpoint& endpoint : kEndpoints) {
        if (endpoint.action == action)
            return QString::fromLatin1(endpoint.path);
    }
    return {};
}

QString actionName(Action action)
{
    for (const Endpoint& endpoint : kEndpoints) {
        if (endpoint.action == action)
            return QString::fromLatin1(endpoint.name);
    }
    return {};
}

bool actionForName(const QString& name, Action* action)
{
    if (!action)
        return false;
    for (const Endpoint& endpoint : kEndpoints) {
        if (name == QString::fromLatin1(endpoint.name)) {
            *action = endpoint.action;
            return true;
        }
    }
    return false;
}

bool isPreparationAction(Action action)
{
    return action >= Action::Espresso && action <= Action::MilkUnlock;
}

bool isSteamPageAction(Action action)
{
    return action == Action::SteamPrepare
        || action == Action::SteamSelectPitcher
        || action == Action::SteamAdjustTime
        || action == Action::SteamStop
        || action == Action::SteamBack;
}

bool isPotentialTarget(const QString& target)
{
    return endpointForTarget(target, true) != nullptr;
}

Result validate(const QString& method, const QString& target, const QByteArray& body,
                const QString& expectedPackageId, const QString& expectedHostId)
{
    const Endpoint* endpoint = endpointForTarget(target, true);
    if (!endpoint)
        return failure(nullptr, 404, QStringLiteral("Unknown session action"));
    if (method != QStringLiteral("POST"))
        return failure(endpoint, 405, QStringLiteral("Use POST"));
    if (target != QString::fromLatin1(endpoint->path))
        return failure(endpoint, 400, QStringLiteral("Query parameters are not allowed"));
    if (body.size() > MaximumBodyBytes)
        return failure(endpoint, 413, QStringLiteral("Action request exceeds 4 KiB"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return failure(endpoint, 400, QStringLiteral("Action request must be a JSON object"));

    const QJsonObject object = document.object();
    const bool selectPitcher = endpoint->action == Action::SelectPitcher;
    const bool steamPagePitcher = endpoint->action == Action::SteamPrepare
        || endpoint->action == Action::SteamSelectPitcher;
    const bool steamPageStop = endpoint->action == Action::SteamStop;
    const bool steamPageBack = endpoint->action == Action::SteamBack;
    const bool steamEditor = endpoint->action == Action::SteamEditor;
    const bool steamGraphTrace = endpoint->action == Action::SteamGraphTrace;
    const bool uiPreference = endpoint->action == Action::UiPreference;
    const bool steamPageAction = steamPagePitcher
        || endpoint->action == Action::SteamAdjustTime || steamPageStop || steamPageBack;
    const int expectedFields = selectPitcher ? 5
        : steamPagePitcher ? 6
        : endpoint->action == Action::SteamAdjustTime || steamPageStop ? 5
        : steamPageBack ? 4 : steamGraphTrace || uiPreference ? 6 : 3;
    // The editor has six envelope fields (identity, operation, revision and
    // control context); the operation-specific field set is checked below.
    if ((!steamEditor && object.size() != expectedFields)
        || !object.contains(QStringLiteral("packageId"))
        || !object.contains(QStringLiteral("hostId"))
        || !object.contains(QStringLiteral("requestId"))
        || (selectPitcher
            && (!object.contains(QStringLiteral("index"))
                || !object.contains(QStringLiteral("steamPitchersRevision"))))
        || (steamPagePitcher
            && (!object.contains(QStringLiteral("index"))
                || !object.contains(QStringLiteral("steamPitchersRevision"))
                || !object.contains(QStringLiteral("controlContextId"))))
        || (endpoint->action == Action::SteamAdjustTime
            && (!object.contains(QStringLiteral("delta"))
                || !object.contains(QStringLiteral("controlContextId"))))
        || (steamPageStop
            && (!object.contains(QStringLiteral("expectedSoftStopped"))
                || !object.contains(QStringLiteral("controlContextId"))))
        || (steamPageBack && !object.contains(QStringLiteral("controlContextId")))
        || (!selectPitcher
            && (object.contains(QStringLiteral("index"))
                || object.contains(QStringLiteral("steamPitchersRevision")))
            && !steamPagePitcher && !steamEditor
            && endpoint->action != Action::SteamAdjustTime)
        || (steamPagePitcher && object.contains(QStringLiteral("delta")))
        || (endpoint->action != Action::SteamAdjustTime && object.contains(QStringLiteral("delta")))
        || (!steamPageStop && object.contains(QStringLiteral("expectedSoftStopped")))
        || (!steamPageAction && !steamEditor && object.contains(QStringLiteral("controlContextId")))
        || (steamEditor && (!object.contains(QStringLiteral("operation"))
                            || !object.contains(QStringLiteral("expectedRevision"))
                            || !object.contains(QStringLiteral("controlContextId"))))
        || (steamGraphTrace && (!object.contains(QStringLiteral("trace"))
                                || !object.contains(QStringLiteral("enabled"))
                                || !object.contains(QStringLiteral("expectedEnabled"))))
        || (uiPreference && (!object.contains(QStringLiteral("preference"))
                             || !object.contains(QStringLiteral("value"))
                             || !object.contains(QStringLiteral("expectedValue"))))) {
        return failure(endpoint, 400, QStringLiteral("Action request has invalid fields"));
    }

    if (steamEditor) {
        const QJsonValue operationValue = object.value(QStringLiteral("operation"));
        const QJsonValue revisionValue = object.value(QStringLiteral("expectedRevision"));
        const QJsonValue contextValue = object.value(QStringLiteral("controlContextId"));
        static const QRegularExpression canonicalContext(
            QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
        if (!operationValue.isString() || !revisionValue.isString()
            || !SteamEditRequest::isRevision(revisionValue.toString())
            || !contextValue.isString()
            || !canonicalContext.match(contextValue.toString()).hasMatch()) {
            return failure(endpoint, 400,
                           QStringLiteral("Steam editor request has invalid operation or identity fields"));
        }
        QJsonObject fields = object;
        fields.remove(QStringLiteral("packageId"));
        fields.remove(QStringLiteral("hostId"));
        fields.remove(QStringLiteral("requestId"));
        fields.remove(QStringLiteral("operation"));
        fields.remove(QStringLiteral("expectedRevision"));
        fields.remove(QStringLiteral("controlContextId"));
        const auto editor = SteamEditRequest::validateFields(operationValue.toString(), fields);
        if (!editor.valid)
            return failure(endpoint, editor.httpStatus, editor.error);
        if (object.size() != fields.size() + 6)
            return failure(endpoint, 400, QStringLiteral("Steam editor request has unknown fields"));
    }
    const auto requireString = [&object](const QString& key, QString* value) {
        const auto field = object.value(key);
        if (!field.isString())
            return false;
        *value = field.toString();
        return !value->isEmpty();
    };

    QString packageId;
    QString hostId;
    QString requestId;
    if (!requireString(QStringLiteral("packageId"), &packageId)
        || !requireString(QStringLiteral("hostId"), &hostId)
        || !requireString(QStringLiteral("requestId"), &requestId)) {
        return failure(endpoint, 400, QStringLiteral("Action request fields must be nonempty strings"));
    }

    Result result;
    result.action = endpoint->action;
    result.actionName = QString::fromLatin1(endpoint->name);
    result.requestId = requestId;
    if (steamGraphTrace) {
        const QJsonValue traceValue = object.value(QStringLiteral("trace"));
        const QJsonValue enabledValue = object.value(QStringLiteral("enabled"));
        const QJsonValue expectedValue = object.value(QStringLiteral("expectedEnabled"));
        const QString trace = traceValue.toString();
        if (!traceValue.isString()
            || (trace != QStringLiteral("pressure") && trace != QStringLiteral("flow")
                && trace != QStringLiteral("temperature"))
            || !enabledValue.isBool() || !expectedValue.isBool()) {
            return failure(endpoint, 400,
                           QStringLiteral("Steam graph trace request has invalid fields"));
        }
        result.graphTrace = trace;
        result.graphTraceEnabled = enabledValue.toBool();
        result.graphTraceExpectedEnabled = expectedValue.toBool();
    }
    if (uiPreference) {
        const QJsonValue preferenceValue = object.value(QStringLiteral("preference"));
        const QJsonValue value = object.value(QStringLiteral("value"));
        const QJsonValue expectedValue = object.value(QStringLiteral("expectedValue"));
        if (!preferenceValue.isString())
            return failure(endpoint, 400, QStringLiteral("UI preference name must be a string"));
        const QString preference = preferenceValue.toString();
        if (preference == QStringLiteral("steamViewMode")) {
            const auto validMode = [](const QJsonValue& candidate) {
                return candidate.isString()
                    && (candidate.toString() == QStringLiteral("timer")
                        || candidate.toString() == QStringLiteral("chart"));
            };
            if (!validMode(value) || !validMode(expectedValue))
                return failure(endpoint, 400, QStringLiteral("Steam view mode is invalid"));
        } else if (preference == QStringLiteral("steamCoachVisualEnabled")
                   || preference == QStringLiteral("steamCoachAudioEnabled")) {
            if (!value.isBool() || !expectedValue.isBool())
                return failure(endpoint, 400, QStringLiteral("Steam coach preference must be boolean"));
        } else if (preference == QStringLiteral("advancedMode")) {
            if (!value.isBool() || !expectedValue.isBool())
                return failure(endpoint, 400, QStringLiteral("Advanced graph preference must be boolean"));
        } else if (preference == QStringLiteral("flowMultiplier")) {
            const auto validMultiplier = [](const QJsonValue& candidate) {
                if (!candidate.isDouble()) return false;
                const double number = candidate.toDouble();
                return std::isfinite(number) && std::floor(number) == number
                    && number >= 1.0 && number <= 3.0;
            };
            if (!validMultiplier(value) || !validMultiplier(expectedValue))
                return failure(endpoint, 400, QStringLiteral("Graph flow multiplier is invalid"));
        } else {
            return failure(endpoint, 400, QStringLiteral("UI preference is not allowlisted"));
        }
        result.uiPreference = preference;
        result.uiPreferenceValue = value;
        result.uiPreferenceExpectedValue = expectedValue;
    }
    if (selectPitcher || steamPagePitcher) {
        const QJsonValue index = object.value(QStringLiteral("index"));
        const double indexNumber = index.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!index.isDouble() || !std::isfinite(indexNumber)
            || std::trunc(indexNumber) != indexNumber
            || indexNumber < std::numeric_limits<int>::min()
            || indexNumber > std::numeric_limits<int>::max()) {
            result.httpStatus = 400;
            result.error = QStringLiteral("index must be an integer");
            return result;
        }
        result.index = static_cast<int>(indexNumber);

        const QJsonValue revision = object.value(QStringLiteral("steamPitchersRevision"));
        static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
        if (!revision.isString() || !sha256.match(revision.toString()).hasMatch()) {
            result.httpStatus = 400;
            result.error = QStringLiteral("steamPitchersRevision must be a lowercase SHA-256 digest");
            return result;
        }
        result.steamPitchersRevision = revision.toString();
    }
    if (steamPageAction) {
        const QJsonValue context = object.value(QStringLiteral("controlContextId"));
        static const QRegularExpression canonicalContext(
            QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
        if (!context.isString() || !canonicalContext.match(context.toString()).hasMatch()) {
            result.httpStatus = 400;
            result.error = QStringLiteral("controlContextId must be an unbraced canonical UUID");
            return result;
        }
        result.controlContextId = context.toString();
    }
    if (steamEditor) {
        result.controlContextId = object.value(QStringLiteral("controlContextId")).toString();
        result.expectedRevision = object.value(QStringLiteral("expectedRevision")).toString();
        result.steamEditor = SteamEditRequest::validateFields(
            object.value(QStringLiteral("operation")).toString(), [&object] {
                QJsonObject fields = object;
                fields.remove(QStringLiteral("packageId"));
                fields.remove(QStringLiteral("hostId"));
                fields.remove(QStringLiteral("requestId"));
                fields.remove(QStringLiteral("operation"));
                fields.remove(QStringLiteral("expectedRevision"));
                fields.remove(QStringLiteral("controlContextId"));
                return fields;
            }());
    }
    if (endpoint->action == Action::SteamAdjustTime) {
        const QJsonValue delta = object.value(QStringLiteral("delta"));
        const double deltaNumber = delta.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!delta.isDouble() || !std::isfinite(deltaNumber)
            || std::trunc(deltaNumber) != deltaNumber
            || (deltaNumber != -5.0 && deltaNumber != 5.0)) {
            result.httpStatus = 400;
            result.error = QStringLiteral("delta must be -5 or 5");
            return result;
        }
        result.delta = static_cast<int>(deltaNumber);
    }
    if (steamPageStop) {
        const QJsonValue expectedSoftStopped = object.value(QStringLiteral("expectedSoftStopped"));
        if (!expectedSoftStopped.isBool()) {
            result.httpStatus = 400;
            result.error = QStringLiteral("expectedSoftStopped must be a boolean");
            return result;
        }
        result.expectedSoftStopped = expectedSoftStopped.toBool();
    }
    if (packageId != expectedPackageId || hostId != expectedHostId) {
        result.httpStatus = 409;
        result.error = QStringLiteral("Action request belongs to a different host package or session");
        return result;
    }

    static const QRegularExpression canonicalUuid(
        QStringLiteral("^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
    if (!canonicalUuid.match(requestId).hasMatch()) {
        result.httpStatus = 400;
        result.error = QStringLiteral("requestId must be an unbraced canonical UUID");
        return result;
    }

    result.requestIdKey = requestId.toLower();
    result.valid = true;
    result.httpStatus = 200;
    result.error.clear();
    return result;
}

} // namespace SessionActionRequest
