#pragma once

// ShotProjection — typed value-type representation of a fully-loaded ShotRecord.
//
// Replaces the stringly-typed QVariantMap that ShotHistoryStorage::convertShotRecord
// used to return. Field access through Q_PROPERTYs gives compile-time safety on the
// C++ side (`shot.finalWeightG` instead of `shot["finalWeightG"].toDouble()`) while
// QML continues to read fields by name (`shotData.finalWeightG`) because the QML
// engine surfaces Q_PROPERTYs of a Q_GADGET as JS properties — the access syntax is
// identical to the previous QVariantMap shape.
//
// Field names match the legacy QVariantMap keys exactly so QML, MCP JSON, and any
// QVariantMap-flavoured consumers (Object.assign in QML, exporter pipeline) stay
// compatible. toVariantMap()/toJsonObject() are provided for the few consumers
// that still need a map (web HTML rendering, JSON serialisation for MCP, the
// visualizer.coffee uploader's JSON builder).

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

#include "history/bagid.h"

struct ShotRecord;

class ShotProjection {
    Q_GADGET

    Q_PROPERTY(qint64 id MEMBER id)
    Q_PROPERTY(QString uuid MEMBER uuid)
    Q_PROPERTY(qint64 timestamp MEMBER timestamp)
    Q_PROPERTY(QString timestampIso MEMBER timestampIso)
    Q_PROPERTY(QString dateTime MEMBER dateTime)
    Q_PROPERTY(QString profileName MEMBER profileName)
    Q_PROPERTY(double durationSec MEMBER durationSec)
    Q_PROPERTY(double finalWeightG MEMBER finalWeightG)
    Q_PROPERTY(double doseWeightG MEMBER doseWeightG)
    Q_PROPERTY(QString beanBrand MEMBER beanBrand)
    Q_PROPERTY(QString beanType MEMBER beanType)
    Q_PROPERTY(int enjoyment0to100 MEMBER enjoyment0to100)
    Q_PROPERTY(bool hasVisualizerUpload MEMBER hasVisualizerUpload)
    Q_PROPERTY(QString beverageType MEMBER beverageType)
    Q_PROPERTY(QString roastDate MEMBER roastDate)
    Q_PROPERTY(QString roastLevel MEMBER roastLevel)
    Q_PROPERTY(QString grinderBrand MEMBER grinderBrand)
    Q_PROPERTY(QString grinderModel MEMBER grinderModel)
    Q_PROPERTY(QString grinderBurrs MEMBER grinderBurrs)
    Q_PROPERTY(QString grinderSetting MEMBER grinderSetting)
    Q_PROPERTY(qlonglong rpm MEMBER rpm)
    // "" = current/none, "older" = superseded by a newer package, "retired" =
    // out of inventory with no successor (add-equipment-packages 4b.7).
    Q_PROPERTY(QString equipmentState MEMBER equipmentState)
    Q_PROPERTY(double drinkTdsPct MEMBER drinkTdsPct)
    Q_PROPERTY(double drinkEyPct MEMBER drinkEyPct)
    Q_PROPERTY(QString espressoNotes MEMBER espressoNotes)
    Q_PROPERTY(QString beanNotes MEMBER beanNotes)
    Q_PROPERTY(QString barista MEMBER barista)
    Q_PROPERTY(QString profileNotes MEMBER profileNotes)
    Q_PROPERTY(QString visualizerId MEMBER visualizerId)
    Q_PROPERTY(QString visualizerUrl MEMBER visualizerUrl)
    Q_PROPERTY(QString debugLog MEMBER debugLog)
    Q_PROPERTY(double temperatureOverrideC MEMBER temperatureOverrideC)
    Q_PROPERTY(double targetWeightG MEMBER targetWeightG)
    Q_PROPERTY(QString stoppedBy MEMBER stoppedBy)
    Q_PROPERTY(QString profileJson MEMBER profileJson)
    Q_PROPERTY(QString profileKbId MEMBER profileKbId)
    Q_PROPERTY(QString beanBaseJson MEMBER beanBaseJson)
    // Coffee bag snapshot (bean-bag-inventory): the bag this shot was pulled
    // with and the beans' freeze lifecycle at shot time (ISO dates, "" = unset).
    // "No bag" sentinel is bagId <= 0 (default -1; the DB read maps NULL to -1)
    // — see bagIdIsSet() in bagid.h for the canonical rule; hasBag() is the
    // ShotProjection-typed form.
    Q_PROPERTY(qint64 bagId MEMBER bagId)
    Q_PROPERTY(QString frozenDate MEMBER frozenDate)
    Q_PROPERTY(QString defrostDate MEMBER defrostDate)

    Q_PROPERTY(bool channelingDetected MEMBER channelingDetected)
    Q_PROPERTY(bool grindIssueDetected MEMBER grindIssueDetected)
    Q_PROPERTY(bool skipFirstFrameDetected MEMBER skipFirstFrameDetected)
    Q_PROPERTY(bool pourTruncatedDetected MEMBER pourTruncatedDetected)

    Q_PROPERTY(QVariantList pressure MEMBER pressure)
    Q_PROPERTY(QVariantList flow MEMBER flow)
    Q_PROPERTY(QVariantList temperature MEMBER temperature)
    Q_PROPERTY(QVariantList temperatureMix MEMBER temperatureMix)
    Q_PROPERTY(QVariantList resistance MEMBER resistance)
    Q_PROPERTY(QVariantList conductance MEMBER conductance)
    Q_PROPERTY(QVariantList darcyResistance MEMBER darcyResistance)
    Q_PROPERTY(QVariantList conductanceDerivative MEMBER conductanceDerivative)
    Q_PROPERTY(QVariantList waterDispensed MEMBER waterDispensed)
    Q_PROPERTY(QVariantList pressureGoal MEMBER pressureGoal)
    Q_PROPERTY(QVariantList flowGoal MEMBER flowGoal)
    Q_PROPERTY(QVariantList temperatureGoal MEMBER temperatureGoal)
    Q_PROPERTY(QVariantList weight MEMBER weight)
    Q_PROPERTY(QVariantList weightFlowRate MEMBER weightFlowRate)

    // summaryLines, detectorResults, phaseSummaries, phases stay loosely typed:
    // they are nested structures or free-form lists that are themselves passed
    // through to QML / MCP without per-field reads. Promoting them to sub-gadgets
    // would multiply the migration surface without adding compile-time safety to
    // any current consumer site.
    Q_PROPERTY(QVariantList summaryLines MEMBER summaryLines)
    Q_PROPERTY(QVariantMap detectorResults MEMBER detectorResults)
    Q_PROPERTY(QVariant phaseSummaries MEMBER phaseSummaries)
    Q_PROPERTY(QVariantList phases MEMBER phases)

public:
    qint64 id = 0;
    QString uuid;
    qint64 timestamp = 0;
    QString timestampIso;
    QString dateTime;
    QString profileName;
    double durationSec = 0.0;
    double finalWeightG = 0.0;
    double doseWeightG = 0.0;
    QString beanBrand;
    QString beanType;
    int enjoyment0to100 = 0;
    bool hasVisualizerUpload = false;
    QString beverageType;
    QString roastDate;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    qint64 rpm = 0;
    QString equipmentState;  // "", "older", or "retired" (add-equipment-packages 4b.7)
    double drinkTdsPct = 0.0;
    double drinkEyPct = 0.0;
    QString espressoNotes;
    QString beanNotes;
    QString barista;
    QString profileNotes;
    QString visualizerId;
    QString visualizerUrl;
    QString debugLog;
    double temperatureOverrideC = 0.0;
    double targetWeightG = 0.0;
    QString stoppedBy;  // #1161: "weight"|"volume"|"manual"|"profileEnd"|""
    QString profileJson;
    QString profileKbId;
    // Compact-JSON linked-bean snapshot ("" = unlinked) — Visualizer canonical
    // or Bean Base sourced; see ShotRecord::beanBaseJson.
    QString beanBaseJson;
    qint64 bagId = -1;
    QString frozenDate;
    QString defrostDate;

    bool channelingDetected = false;
    bool grindIssueDetected = false;
    bool skipFirstFrameDetected = false;
    bool pourTruncatedDetected = false;

    QVariantList pressure;
    QVariantList flow;
    QVariantList temperature;
    QVariantList temperatureMix;
    QVariantList resistance;
    QVariantList conductance;
    QVariantList darcyResistance;
    QVariantList conductanceDerivative;
    QVariantList waterDispensed;
    QVariantList pressureGoal;
    QVariantList flowGoal;
    QVariantList temperatureGoal;
    QVariantList weight;
    QVariantList weightFlowRate;

    QVariantList summaryLines;
    QVariantMap detectorResults;
    QVariant phaseSummaries;
    QVariantList phases;

    // True when this projection holds a real shot (id != 0). convertShotRecord()
    // returns a default-constructed ShotProjection on lookup failure; isValid()
    // is the cheapest reliable way to detect that.
    bool isValid() const { return id != 0; }

    // "Is this shot linked to a coffee bag?" — the ShotProjection-typed form of
    // the shared bagIdIsSet() predicate. Q_INVOKABLE so QML can ask the same.
    Q_INVOKABLE bool hasBag() const { return bagIdIsSet(bagId); }

    // Build the legacy QVariantMap shape — same keys, same nested types as the
    // pre-Q_GADGET convertShotRecord() return value. Today's only callers are
    // toJsonObject() (delegates here) and tests. Direct C++ consumers in the
    // shot-list HTML page and visualizer exporter read fields off the typed
    // projection instead.
    QVariantMap toVariantMap() const;

    // QJsonObject form for MCP (shots_get_detail, shots_compare). Internally
    // delegates to QJsonObject::fromVariantMap(toVariantMap()).
    QJsonObject toJsonObject() const;

    // Build a ShotProjection from the legacy QVariantMap shape (or any JS
    // object QML hands across a Q_INVOKABLE boundary). Used by Q_INVOKABLE
    // methods that take `const ShotProjection&`: a registered QMetaType
    // converter (see registerMetaTypeConverters() called once at startup)
    // routes QVariantMap → ShotProjection through this method, so QML callers
    // passing a JS object — including the plain-JS clone produced by
    // PostShotReviewPage's clonePersistedShot (used in onShotBadgesUpdated and
    // saveEditedShot) — still satisfy the parameter.
    static ShotProjection fromVariantMap(const QVariantMap& map);

    // Coerce a QVariant arg (from a Q_INVOKABLE taking `const QVariant&`) into a
    // ShotProjection, accepting BOTH shapes QML may hand across the boundary:
    //   - a genuine ShotProjection gadget (e.g. the raw shotReady() wrapper), or
    //   - a plain JS object → QVariantMap (e.g. PostShotReviewPage's
    //     clonePersistedShot result after a badge update / metadata edit).
    // The registered QVariantMap→ShotProjection metatype converter does NOT
    // reliably engage on the Qt 6.11 QML→C++ invokable argument-binding path
    // (it throws "Could not convert argument from [object Object] to
    // ShotProjection"), so invokables that may receive an edited/cloned shot
    // take `const QVariant&` and coerce here instead. Same approach as
    // AIManager's coerceShot() (#1298).
    static ShotProjection coerce(const QVariant& v);

    // Register the QVariantMap → ShotProjection converter. Call once at
    // application startup (main.cpp). Idempotent across calls.
    static void registerMetaTypeConverters();
};

Q_DECLARE_METATYPE(ShotProjection)
