#include "decentshotrecord.h"

#include "history/shotprojection.h"
#include "network/roastdate.h"
#include "network/shotpayloadhelpers.h"
#include "version.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace {

QVector<QPointF> toPoints(const QVariantList& points) {
    QVector<QPointF> out;
    out.reserve(points.size());
    for (const QVariant& p : points) {
        const QVariantMap m = p.toMap();
        out.append(QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()));
    }
    return out;
}

QString isoUtcMs(qint64 msSinceEpoch) {
    return QDateTime::fromMSecsSinceEpoch(msSinceEpoch, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

void putString(QJsonObject& obj, const QString& key, const QString& value) {
    if (!value.trimmed().isEmpty()) obj[key] = value;
}

void putPositive(QJsonObject& obj, const QString& key, double value) {
    if (value > 0) obj[key] = value;
}

// Frame number of the last phase marker at or before each sample, as
// de1app's converter derives profileFrame from the step boundaries.
QVector<int> frameForSamples(const QVariantList& phases, const QVector<QPointF>& timeline) {
    QVector<QPair<double, int>> markers;
    for (const QVariant& p : phases) {
        const QVariantMap m = p.toMap();
        const int frame = m.value(QStringLiteral("frameNumber"), -1).toInt();
        if (frame >= 0) markers.append({m.value(QStringLiteral("time")).toDouble(), frame});
    }
    QVector<int> frames;
    frames.reserve(timeline.size());
    qsizetype next = 0;
    int current = 0;
    for (const QPointF& pt : timeline) {
        while (next < markers.size() && markers[next].first <= pt.x()) current = markers[next++].second;
        frames.append(current);
    }
    return frames;
}

}  // namespace

QString DecentShotRecord::modelName(int machineModel) {
    switch (machineModel) {
        case 1: return QStringLiteral("DE1");
        case 2: return QStringLiteral("DE1+");
        case 3: return QStringLiteral("DE1PRO");
        case 4: return QStringLiteral("DE1XL");
        case 5: return QStringLiteral("DE1CAFE");
        default: return QString();
    }
}

QByteArray DecentShotRecord::build(const ShotProjection& shot, const DecentMachineIdentity& machine) {
    // The saved timestamp, as Decenza's history and the Visualizer upload use it,
    // so the shot shows the same time on every surface.
    const qint64 baseMs = shot.timestamp * 1000;

    // Pressure is the master timeline (as in the Visualizer payload); every other
    // series is resampled onto it so the measurement columns stay aligned.
    const QVector<QPointF> pressure = toPoints(shot.pressure);
    const QJsonArray flow = interpolateGoalData(toPoints(shot.flow), pressure);
    const QJsonArray basket = interpolateGoalData(toPoints(shot.temperature), pressure);
    const QJsonArray mix = interpolateGoalData(toPoints(shot.temperatureMix), pressure);
    const QJsonArray pressureGoal = interpolateGoalData(toPoints(shot.pressureGoal), pressure);
    const QJsonArray flowGoal = interpolateGoalData(toPoints(shot.flowGoal), pressure);
    const QJsonArray tempGoal = interpolateGoalData(toPoints(shot.temperatureGoal), pressure);
    const QJsonArray mixGoal = shot.temperatureMixGoal.isEmpty()
        ? tempGoal : interpolateGoalData(toPoints(shot.temperatureMixGoal), pressure);
    const bool hasScale = !shot.weight.isEmpty();
    const QJsonArray weight = interpolateGoalData(toPoints(shot.weight), pressure);
    const QJsonArray weightFlow = interpolateGoalData(toPoints(shot.weightFlowRate), pressure);
    const QVector<int> frames = frameForSamples(shot.phases, pressure);

    QJsonArray measurements;
    for (qsizetype i = 0; i < pressure.size(); ++i) {
        const QString ts = isoUtcMs(baseMs + qRound64(pressure[i].x() * 1000.0));
        QJsonObject m;
        m["timestamp"] = ts;
        m["state"] = QJsonObject{{"state", "espresso"}, {"substate", "pouring"}};
        m["flow"] = flow[i];
        m["pressure"] = pressure[i].y();
        m["targetFlow"] = flowGoal[i];
        m["targetPressure"] = pressureGoal[i];
        m["mixTemperature"] = mix[i];
        m["groupTemperature"] = basket[i];
        m["targetMixTemperature"] = mixGoal[i];
        m["targetGroupTemperature"] = tempGoal[i];
        m["profileFrame"] = frames[i];
        m["steamTemperature"] = 0;
        QJsonObject sample{{"machine", m}};
        if (hasScale) {
            sample["scale"] = QJsonObject{{"timestamp", ts}, {"weight", weight[i]}, {"weightFlow", weightFlow[i]},
                                          {"battery", QJsonValue::Null}, {"timerValue", QJsonValue::Null}};
        }
        measurements.append(sample);
    }

    QJsonObject context;
    putPositive(context, "targetDoseWeight", shot.doseWeightG);
    putPositive(context, "targetYield", shot.targetWeightG);
    putString(context, "grinderModel", grinderDisplayName(shot.grinderBrand, shot.grinderModel));
    putString(context, "grinderSetting", shot.grinderSetting);
    putString(context, "coffeeName", shot.beanType);
    putString(context, "coffeeRoaster", shot.beanBrand);
    putString(context, "finalBeverageType", shot.beverageType);
    putString(context, "baristaName", shot.barista);
    QJsonObject extras;
    const QString roastDate = RoastDate::toIso(shot.roastDate);
    if (QDate::fromString(roastDate, Qt::ISODate).isValid()) extras["roastDate"] = roastDate;
    putString(extras, "roastLevel", shot.roastLevel);
    if (shot.rpm > 0) extras["grinderRpm"] = shot.rpm;
    if (!extras.isEmpty()) context["extras"] = extras;

    QJsonObject annotations;
    putPositive(annotations, "actualDoseWeight", shot.doseWeightG);
    putPositive(annotations, "actualYield", shot.finalWeightG);
    putPositive(annotations, "drinkTds", shot.drinkTdsPct);
    putPositive(annotations, "drinkEy", shot.drinkEyPct);
    if (shot.enjoyment0to100 > 0) annotations["enjoyment"] = shot.enjoyment0to100;
    putString(annotations, "espressoNotes", shot.espressoNotes);

    // The stored profile snapshot verbatim (already de1app v2), for the reason
    // VisualizerUploader::buildHistoryShotJson gives: re-serializing through
    // Profile would make a historical shot claim values it never ran.
    QJsonObject profile = QJsonDocument::fromJson(shot.profileJson.toUtf8()).object();
    if (profile.isEmpty())
        profile = QJsonObject{{"version", "2"}, {"title", shot.profileName}, {"steps", QJsonArray()}};

    QJsonObject workflow;
    workflow["id"] = QStringLiteral("decenza-wf-") + shot.uuid;
    workflow["name"] = shot.profileName;
    workflow["description"] = QJsonValue::Null;
    workflow["profile"] = profile;
    workflow["context"] = context;
    workflow["steamSettings"] = QJsonObject();
    workflow["hotWaterData"] = QJsonObject();
    workflow["rinseData"] = QJsonObject();

    QJsonObject machineObj;
    putString(machineObj, "serialNumber", machine.serialNumber);
    putString(machineObj, "firmwareVersion", machine.firmwareVersion);
    putString(machineObj, "model", machine.model);

    QJsonObject root;
    root["id"] = shot.uuid;
    root["timestamp"] = isoUtcMs(baseMs);
    root["measurements"] = measurements;
    root["workflow"] = workflow;
    root["annotations"] = annotations;
    root["machine"] = machineObj;
    root["app"] = QJsonObject{{"name", "decenza"}, {"version", QStringLiteral(VERSION_STRING)},
                              {"sourceFormat", "decenza"}};
    root["schemaVersion"] = 1;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
