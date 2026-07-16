#include "shotfileparser.h"
#include "core/grinderaliases.h"
#include "network/tastecvamap.h"
#include <QFile>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>

namespace {
// visualizer.coffee serialises telemetry samples as JSON *strings*
// ("0.0", "9.42") — the same Tcl-huddle convention the profile JSON uses.
// Convert an array of them to doubles, tolerating already-numeric values.
QVector<double> jsonArrayToDoubles(const QJsonArray& arr)
{
    QVector<double> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (v.isDouble()) {
            out.append(v.toDouble());
        } else if (v.isString()) {
            bool ok = false;
            double d = v.toString().toDouble(&ok);
            if (ok) out.append(d);
            else out.append(0.0);  // keep alignment with the timeframe array
        } else {
            out.append(0.0);
        }
    }
    return out;
}

// Scalar sibling of the above. visualizer.coffee string-encodes DYE scalars too
// ("16.2", "0"), while a few (espresso_enjoyment) arrive as bare numbers.
// QJsonValue::toDouble()/toInt() return 0 for a *string* value — no coercion —
// so a bare .toDouble() silently zeroes every string-encoded field. Branch on
// the type (mirrors jsonToDouble in profileframe.cpp).
double jsonToScalar(const QJsonValue& v, double defaultVal = 0.0)
{
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : defaultVal;
    }
    return v.toDouble(defaultVal);
}
}  // namespace

ShotFileParser::ParseResult ShotFileParser::parse(const QByteArray& fileContents, const QString& filename)
{
    ParseResult result;
    QString content = QString::fromUtf8(fileContents);

    // Extract timestamp from clock field, falling back to the filename
    // (de1app names files YYYYMMDDTHHMMSS.shot, so the name is always a valid timestamp)
    QString clockStr = extractValue(content, "clock");
    qint64 timestamp = clockStr.toLongLong();

    if (timestamp == 0) {
        // Try to parse timestamp from filename: YYYYMMDDTHHMMSS[.shot]
        QString base = filename.section('.', 0, 0);  // strip extension
        QDateTime dt = QDateTime::fromString(base, "yyyyMMddTHHmmss");
        if (dt.isValid()) {
            // Interpret the parsed date+time as UTC (de1app filenames are UTC-based).
            // setTimeZone() relabels without converting; construct explicitly as UTC instead.
            QDateTime utcDt(dt.date(), dt.time(), QTimeZone::utc());
            timestamp = utcDt.toSecsSinceEpoch();
            qDebug() << "ShotFileParser: no clock field in" << filename << "- derived timestamp from filename:" << timestamp;
        }
    }

    if (timestamp == 0) {
        result.errorMessage = clockStr.isEmpty() ? "Missing clock timestamp" : "Invalid clock timestamp";
        return result;
    }

    result.record.summary.timestamp = timestamp;
    result.record.summary.uuid = generateUuid(timestamp, filename);

    // Extract time-series data
    QVector<double> elapsed = parseTclList(extractValue(content, "espresso_elapsed"));
    if (elapsed.isEmpty()) {
        result.errorMessage = "Missing espresso_elapsed data";
        return result;
    }

    // Core time-series
    QVector<double> pressure = parseTclList(extractValue(content, "espresso_pressure"));
    QVector<double> flow = parseTclList(extractValue(content, "espresso_flow"));
    QVector<double> tempBasket = parseTclList(extractValue(content, "espresso_temperature_basket"));
    QVector<double> weight = parseTclList(extractValue(content, "espresso_weight"));

    // Goal/target values
    QVector<double> pressureGoal = parseTclList(extractValue(content, "espresso_pressure_goal"));
    QVector<double> flowGoal = parseTclList(extractValue(content, "espresso_flow_goal"));
    QVector<double> tempGoal = parseTclList(extractValue(content, "espresso_temperature_goal"));

    // Additional data (de1app records these)
    QVector<double> tempMix = parseTclList(extractValue(content, "espresso_temperature_mix"));
    QVector<double> resistance = parseTclList(extractValue(content, "espresso_resistance"));
    QVector<double> waterDispensed = parseTclList(extractValue(content, "espresso_water_dispensed"));

    // Scale-based flow rate (g/s) - important for visualizer and weight-flow graphs
    QVector<double> flowWeight = parseTclList(extractValue(content, "espresso_flow_weight"));

    // Convert to point vectors
    result.record.pressure = toPointVector(elapsed, pressure);
    result.record.flow = toPointVector(elapsed, flow);
    result.record.temperature = toPointVector(elapsed, tempBasket);
    result.record.weight = toPointVector(elapsed, weight);
    result.record.pressureGoal = toPointVector(elapsed, pressureGoal);
    result.record.flowGoal = toPointVector(elapsed, flowGoal);
    result.record.temperatureGoal = toPointVector(elapsed, tempGoal);
    if (!tempMix.isEmpty())
        result.record.temperatureMix = toPointVector(elapsed, tempMix);
    if (!resistance.isEmpty())
        result.record.resistance = toPointVector(elapsed, resistance);
    if (!waterDispensed.isEmpty()) {
        // de1app stores espresso_water_dispensed at 0.1× scale (tenths of ml); normalize to actual ml
        // so that all shots in the DB use the same unit regardless of import source.
        for (auto& v : waterDispensed)
            v *= 10.0;
        result.record.waterDispensed = toPointVector(elapsed, waterDispensed);
    }
    if (!flowWeight.isEmpty())
        result.record.weightFlowRate = toPointVector(elapsed, flowWeight);

    // Duration from last elapsed time
    result.record.summary.duration = elapsed.isEmpty() ? 0 : elapsed.last();

    // Parse settings block for metadata
    QString settingsBlock = extractBracedBlock(content, "settings");
    if (!settingsBlock.isEmpty()) {
        QVariantMap settings = parseTclDict(settingsBlock);

        result.record.summary.profileName = settings.value("profile_title", "Unknown").toString();
        result.record.summary.beanBrand = settings.value("bean_brand").toString();
        result.record.summary.beanType = settings.value("bean_type").toString();
        result.record.roastDate = settings.value("roast_date").toString();
        result.record.roastLevel = settings.value("roast_level").toString();
        QString rawGrinder = settings.value("grinder_model").toString();
        auto grinderLookup = GrinderAliases::lookup(rawGrinder);
        if (grinderLookup.found) {
            result.record.grinderBrand = grinderLookup.brand;
            result.record.grinderModel = grinderLookup.model;
            result.record.grinderBurrs = grinderLookup.stockBurrs;
        } else {
            result.record.grinderModel = rawGrinder;
        }
        result.record.grinderSetting = settings.value("grinder_setting").toString();
        result.record.drinkTds = settings.value("drink_tds").toDouble();
        result.record.drinkEy = settings.value("drink_ey").toDouble();
        result.record.summary.enjoyment = settings.value("espresso_enjoyment").toInt();
        result.record.espressoNotes = settings.value("espresso_notes").toString();
        result.record.barista = settings.value("my_name", settings.value("drinker_name")).toString();
        result.record.summary.doseWeight = settings.value("grinder_dose_weight").toDouble();
        result.record.summary.finalWeight = settings.value("drink_weight").toDouble();
        result.record.summary.beverageType = settings.value("beverage_type", "espresso").toString();
        result.record.beanNotes = settings.value("bean_notes").toString();
        result.record.profileNotes = settings.value("profile_notes").toString();
    }

    // If final weight is 0 but we have weight data, use the last weight value
    if (result.record.summary.finalWeight <= 0 && !result.record.weight.isEmpty()) {
        // Find max weight (in case last sample isn't the highest)
        double maxWeight = 0;
        for (const auto& pt : result.record.weight) {
            if (pt.y() > maxWeight) maxWeight = pt.y();
        }
        result.record.summary.finalWeight = maxWeight;
    }

    // Extract profile JSON
    result.record.profileJson = extractProfileJson(content);

    // Parse phase markers from timers
    QString preinfStartStr = extractValue(content, "timers(espresso_preinfusion_start)");
    QString preinfStopStr = extractValue(content, "timers(espresso_preinfusion_stop)");
    QString pourStartStr = extractValue(content, "timers(espresso_pour_start)");
    QString espressoStartStr = extractValue(content, "timers(espresso_start)");

    qint64 espressoStart = espressoStartStr.toLongLong();
    qint64 preinfStart = preinfStartStr.toLongLong();
    qint64 preinfStop = preinfStopStr.toLongLong();
    qint64 pourStart = pourStartStr.toLongLong();

    if (espressoStart > 0) {
        // Preinfusion start
        if (preinfStart > 0 && preinfStart >= espressoStart) {
            HistoryPhaseMarker marker;
            marker.time = (preinfStart - espressoStart) / 1000.0;
            marker.label = "Preinfusion";
            marker.isFlowMode = true;
            result.record.phases.append(marker);
        }

        // Pour start (end of preinfusion)
        if (pourStart > 0 && pourStart > espressoStart) {
            HistoryPhaseMarker marker;
            marker.time = (pourStart - espressoStart) / 1000.0;
            marker.label = "Pour";
            marker.isFlowMode = false;
            result.record.phases.append(marker);
        }
    }

    result.success = true;
    return result;
}

ShotFileParser::ParseResult ShotFileParser::parseFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ParseResult result;
        result.errorMessage = QString("Cannot open file: %1").arg(file.errorString());
        return result;
    }

    QString filename = filePath.section('/', -1).section('\\', -1);
    return parse(file.readAll(), filename);
}

ShotFileParser::ParseResult ShotFileParser::parseVisualizerShot(const QJsonObject& shotJson,
                                                                const QString& profileJson,
                                                                const QString& visualizerId,
                                                                qint64 clockEpoch)
{
    ParseResult result;

    // The download response's `clock` is null and `start_time` is an ISO
    // string, so the authoritative start time comes from the shot-list
    // entry the caller already fetched. Fall back to parsing start_time.
    qint64 timestamp = clockEpoch;
    if (timestamp <= 0) {
        const QString startStr = shotJson.value("start_time").toString();
        if (!startStr.isEmpty()) {
            QDateTime dt = QDateTime::fromString(startStr, Qt::ISODate);
            if (dt.isValid()) timestamp = dt.toSecsSinceEpoch();
        }
    }
    if (timestamp <= 0) {
        result.errorMessage = "Missing shot start time";
        return result;
    }

    result.record.summary.timestamp = timestamp;
    // A deterministic uuid keyed on the Visualizer id makes re-runs of the
    // recovery idempotent (importShotRecord dedupes on uuid first). Feed the
    // visualizer id as the "filename" so it stays stable across runs.
    //
    // The sync-gap case — a shot pulled ON THIS DEVICE has a local uuid keyed on
    // its FILENAME, so this visualizer-id-keyed uuid can't match it — is handled
    // by importShotRecordStatic's dedicated visualizer_id dedupe probe: it
    // compares record.visualizerId (set below) against the local shot's
    // visualizer_id column (populated when that shot was uploaded), the one
    // identifier guaranteed identical on both sides. So dedupe no longer has to
    // rely on the weaker timestamp + profile_title fallback for these.
    result.record.summary.uuid = generateUuid(timestamp, visualizerId);
    result.record.visualizerId = visualizerId;

    const QJsonObject data = shotJson.value("data").toObject();
    const QVector<double> elapsed = jsonArrayToDoubles(shotJson.value("timeframe").toArray());
    if (elapsed.isEmpty()) {
        result.errorMessage = "Missing timeframe data";
        return result;
    }

    // Core + goal + auxiliary series. Keys mirror the DE1 .shot field names
    // (the visualizer download echoes them verbatim).
    const QVector<double> pressure     = jsonArrayToDoubles(data.value("espresso_pressure").toArray());
    // Every espresso shot has a pressure series. If timeframe is present but the
    // telemetry object is missing/empty/renamed (a partial upload, or a schema
    // change on visualizer.coffee), fail loudly rather than importing a hollow
    // shot with blank traces that would still be counted as "imported".
    if (pressure.isEmpty()) {
        result.errorMessage = "Missing pressure samples";
        return result;
    }
    // A pressure series materially shorter than the timeframe means a partial /
    // truncated upload: toPointVector zips with qMin, so the shot still imports
    // but with a chopped-off trace. That's better than dropping it, but log so a
    // systematic upstream truncation is diagnosable rather than silent.
    if (pressure.size() < elapsed.size() - 1) {
        qWarning() << "parseVisualizerShot: pressure series truncated for" << visualizerId
                   << "-" << pressure.size() << "of" << elapsed.size()
                   << "samples; importing partial trace";
    }
    const QVector<double> flow         = jsonArrayToDoubles(data.value("espresso_flow").toArray());
    const QVector<double> tempBasket   = jsonArrayToDoubles(data.value("espresso_temperature_basket").toArray());
    const QVector<double> weight       = jsonArrayToDoubles(data.value("espresso_weight").toArray());
    const QVector<double> pressureGoal = jsonArrayToDoubles(data.value("espresso_pressure_goal").toArray());
    const QVector<double> flowGoal     = jsonArrayToDoubles(data.value("espresso_flow_goal").toArray());
    const QVector<double> tempGoal     = jsonArrayToDoubles(data.value("espresso_temperature_goal").toArray());
    const QVector<double> tempMix      = jsonArrayToDoubles(data.value("espresso_temperature_mix").toArray());
    const QVector<double> resistance   = jsonArrayToDoubles(data.value("espresso_resistance").toArray());
    QVector<double> waterDisp          = jsonArrayToDoubles(data.value("espresso_water_dispensed").toArray());
    const QVector<double> flowWeight   = jsonArrayToDoubles(data.value("espresso_flow_weight").toArray());

    result.record.pressure        = toPointVector(elapsed, pressure);
    result.record.flow            = toPointVector(elapsed, flow);
    result.record.temperature     = toPointVector(elapsed, tempBasket);
    result.record.weight          = toPointVector(elapsed, weight);
    result.record.pressureGoal    = toPointVector(elapsed, pressureGoal);
    result.record.flowGoal        = toPointVector(elapsed, flowGoal);
    result.record.temperatureGoal = toPointVector(elapsed, tempGoal);
    if (!tempMix.isEmpty())
        result.record.temperatureMix = toPointVector(elapsed, tempMix);
    if (!resistance.isEmpty())
        result.record.resistance = toPointVector(elapsed, resistance);
    if (!waterDisp.isEmpty()) {
        // visualizer.coffee stores espresso_water_dispensed at the same 0.1x
        // (tenths-of-ml) scale de1app .shot files use — Decenza's uploader
        // multiplies the DB's real-ml values by 0.1 on the way up (see
        // VisualizerUploader::buildHistoryShotJson). Apply the identical x10
        // the Tcl import path does so recovered shots match natively-stored
        // ones (the DB unit is real ml).
        for (auto& v : waterDisp)
            v *= 10.0;
        result.record.waterDispensed = toPointVector(elapsed, waterDisp);
    }
    if (!flowWeight.isEmpty())
        result.record.weightFlowRate = toPointVector(elapsed, flowWeight);

    // Duration: prefer the reported value, else the last elapsed sample.
    double duration = jsonToScalar(shotJson.value("duration"));
    if (duration <= 0) duration = elapsed.isEmpty() ? 0 : elapsed.last();
    result.record.summary.duration = duration;

    // DYE metadata (flat on the download response).
    result.record.summary.profileName = shotJson.value("profile_title").toString();
    if (result.record.summary.profileName.isEmpty())
        result.record.summary.profileName = "Unknown";
    result.record.summary.beanBrand = shotJson.value("bean_brand").toString();
    result.record.summary.beanType  = shotJson.value("bean_type").toString();
    result.record.roastDate  = shotJson.value("roast_date").toString();
    result.record.roastLevel = shotJson.value("roast_level").toString();

    const QString rawGrinder = shotJson.value("grinder_model").toString();
    auto grinderLookup = GrinderAliases::lookup(rawGrinder);
    if (grinderLookup.found) {
        result.record.grinderBrand = grinderLookup.brand;
        result.record.grinderModel = grinderLookup.model;
        result.record.grinderBurrs = grinderLookup.stockBurrs;
    } else {
        result.record.grinderModel = rawGrinder;
    }
    result.record.grinderSetting = shotJson.value("grinder_setting").toString();
    result.record.drinkTds = jsonToScalar(shotJson.value("drink_tds"));
    result.record.drinkEy  = jsonToScalar(shotJson.value("drink_ey"));
    result.record.summary.enjoyment = qRound(jsonToScalar(shotJson.value("espresso_enjoyment")));
    result.record.espressoNotes = shotJson.value("espresso_notes").toString();
    result.record.beanNotes = shotJson.value("bean_notes").toString();
    result.record.barista = shotJson.value("barista").toString();
    result.record.summary.doseWeight = jsonToScalar(shotJson.value("bean_weight"));
    result.record.summary.finalWeight = jsonToScalar(shotJson.value("drink_weight"));
    result.record.summary.beverageType = "espresso";

    // Structured taste axes: reverse the CVA mapping the uploader applies
    // (tasteBalance/tasteBody → acidity/bitterness/mouthfeel) so a recovered
    // shot keeps its taste dial-in. See applyTasteCvaMapping in tastecvamap.h.
    const int acidity    = qRound(jsonToScalar(shotJson.value("acidity")));
    const int bitterness = qRound(jsonToScalar(shotJson.value("bitterness")));
    const int mouthfeel  = qRound(jsonToScalar(shotJson.value("mouthfeel")));
    result.record.tasteBalance = cvaToTasteBalance(acidity, bitterness);
    result.record.tasteBody    = cvaToTasteBody(mouthfeel);

    // If final weight is unset but we recorded weight samples, use the peak.
    if (result.record.summary.finalWeight <= 0 && !result.record.weight.isEmpty()) {
        double maxWeight = 0;
        for (const auto& pt : result.record.weight)
            if (pt.y() > maxWeight) maxWeight = pt.y();
        result.record.summary.finalWeight = maxWeight;
    }

    // Frame boundaries: the download has no de1app timers block, but it does
    // carry espresso_state_change — a per-sample array that flips sign
    // (+/-10000000) at each frame transition (the same convention Decenza
    // uploads; see VISUALIZER.md "state_change Array"). Emit a phase marker
    // at each sign change so the shot-detail view still draws frame lines.
    const QVector<double> stateChange = jsonArrayToDoubles(data.value("espresso_state_change").toArray());
    if (stateChange.size() >= 2 && stateChange.size() <= elapsed.size()) {
        int frameNumber = 0;
        // Real downloads start the series at 0.0; seed the reference sign from the
        // first *non-zero* sample so the leading 0 -> ±1e7 step isn't recorded as
        // a phantom frame boundary at t≈0.
        bool havePrev = false;
        bool prevPos = false;
        for (qsizetype i = 0; i < stateChange.size(); ++i) {
            if (stateChange[i] == 0.0)
                continue;
            const bool curPos = stateChange[i] > 0;
            if (!havePrev) {
                prevPos = curPos;
                havePrev = true;
                continue;
            }
            if (prevPos != curPos) {
                HistoryPhaseMarker marker;
                marker.time = elapsed[i];
                marker.frameNumber = ++frameNumber;
                result.record.phases.append(marker);
                prevPos = curPos;
            }
        }
    } else if (!stateChange.isEmpty()) {
        // Present but unusable (fewer than 2 samples, or longer than the
        // timeframe => misaligned). The shot imports fine but the detail view
        // draws no frame lines; log so a schema drift is diagnosable.
        qWarning() << "parseVisualizerShot: unusable espresso_state_change for" << visualizerId
                   << "(" << stateChange.size() << "samples vs" << elapsed.size()
                   << "timeframe); no frame markers";
    }

    // Profile JSON is fetched separately (the download carries only a URL).
    if (!profileJson.isEmpty()) {
        QJsonDocument pdoc = QJsonDocument::fromJson(profileJson.toUtf8());
        if (!pdoc.isNull())
            result.record.profileJson = profileJson;
        else
            qWarning() << "parseVisualizerShot: malformed profile JSON for"
                       << visualizerId << "- importing shot without a profile";
    }

    result.success = true;
    return result;
}

QVector<double> ShotFileParser::parseTclList(const QString& listStr)
{
    QVector<double> result;
    QString str = listStr.trimmed();

    // Remove outer braces if present
    if (str.startsWith('{') && str.endsWith('}')) {
        str = str.mid(1, str.length() - 2);
    }

    // Split by whitespace
    QStringList parts = str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    result.reserve(parts.size());
    for (const QString& part : parts) {
        bool ok;
        double val = part.toDouble(&ok);
        if (ok) {
            result.append(val);
        }
    }

    return result;
}

QVariantMap ShotFileParser::parseTclDict(const QString& dictStr)
{
    QVariantMap result;
    QString str = dictStr.trimmed();

    // Remove outer braces
    if (str.startsWith('{') && str.endsWith('}')) {
        str = str.mid(1, str.length() - 2);
    }

    int pos = 0;
    while (pos < str.length()) {
        // Skip whitespace
        while (pos < str.length() && str[pos].isSpace()) pos++;
        if (pos >= str.length()) break;

        // Read key
        QString key;
        while (pos < str.length() && !str[pos].isSpace() && str[pos] != '{') {
            key += str[pos++];
        }

        // Skip whitespace
        while (pos < str.length() && str[pos].isSpace()) pos++;
        if (pos >= str.length()) break;

        // Read value
        QString value;
        if (str[pos] == '{') {
            // Braced value - find matching close brace
            int braceCount = 1;
            pos++; // Skip opening brace
            int valueStart = pos;
            while (pos < str.length() && braceCount > 0) {
                if (str[pos] == '{') braceCount++;
                else if (str[pos] == '}') braceCount--;
                pos++;
            }
            value = str.mid(valueStart, pos - valueStart - 1);
        } else {
            // Unbraced value - read until whitespace
            while (pos < str.length() && !str[pos].isSpace()) {
                value += str[pos++];
            }
        }

        if (!key.isEmpty()) {
            result[key] = value;
        }
    }

    return result;
}

QString ShotFileParser::extractValue(const QString& content, const QString& key)
{
    // Match patterns like: key {value} or key value
    QRegularExpression re(QString("^%1\\s+(.+)$").arg(QRegularExpression::escape(key)),
                          QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = re.match(content);

    if (match.hasMatch()) {
        QString value = match.captured(1).trimmed();

        // Handle braced values
        if (value.startsWith('{')) {
            int braceCount = 1;
            int pos = 1;
            while (pos < value.length() && braceCount > 0) {
                if (value[pos] == '{') braceCount++;
                else if (value[pos] == '}') braceCount--;
                pos++;
            }
            return value.left(pos);
        }

        // Simple value - return first word
        return value.split(QRegularExpression("\\s+")).first();
    }

    return QString();
}

QString ShotFileParser::extractBracedBlock(const QString& content, const QString& key)
{
    // Find the key followed by a brace block
    qsizetype keyPos = content.indexOf(QRegularExpression(QString("^%1\\s+\\{").arg(QRegularExpression::escape(key)),
                                                     QRegularExpression::MultilineOption));
    if (keyPos < 0) return QString();

    // Find the opening brace
    qsizetype braceStart = content.indexOf('{', keyPos);
    if (braceStart < 0) return QString();

    // Find matching closing brace
    int braceCount = 1;
    qsizetype pos = braceStart + 1;
    while (pos < content.length() && braceCount > 0) {
        if (content[pos] == '{') braceCount++;
        else if (content[pos] == '}') braceCount--;
        pos++;
    }

    return content.mid(braceStart, pos - braceStart);
}

QVector<QPointF> ShotFileParser::toPointVector(const QVector<double>& times, const QVector<double>& values)
{
    QVector<QPointF> result;
    qsizetype count = qMin(times.size(), values.size());
    result.reserve(count);

    for (qsizetype i = 0; i < count; ++i) {
        // Filter out invalid goal values (-1 means "no goal for this mode")
        if (values[i] >= 0) {
            result.append(QPointF(times[i], values[i]));
        }
    }

    return result;
}

QString ShotFileParser::extractProfileJson(const QString& content)
{
    // The profile is stored as a JSON block after "profile {"
    qsizetype profileStart = content.indexOf(QRegularExpression("^profile\\s+\\{", QRegularExpression::MultilineOption));
    if (profileStart < 0) return QString();

    qsizetype jsonStart = content.indexOf('{', profileStart);
    if (jsonStart < 0) return QString();

    // Find matching closing brace
    int braceCount = 1;
    qsizetype pos = jsonStart + 1;
    while (pos < content.length() && braceCount > 0) {
        if (content[pos] == '{') braceCount++;
        else if (content[pos] == '}') braceCount--;
        pos++;
    }

    QString jsonStr = content.mid(jsonStart, pos - jsonStart);

    // Validate it's actually JSON
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isNull()) return QString();

    return jsonStr;
}

QString ShotFileParser::generateUuid(qint64 timestamp, const QString& filename)
{
    // Generate deterministic UUID from timestamp + filename
    // This allows detecting duplicates when re-importing
    QByteArray data;
    data.append(QByteArray::number(timestamp));
    data.append(filename.toUtf8());

    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);

    // Format as UUID (take first 16 bytes of hash)
    QString uuid = QString("%1-%2-%3-%4-%5")
        .arg(QString(hash.mid(0, 4).toHex()))
        .arg(QString(hash.mid(4, 2).toHex()))
        .arg(QString(hash.mid(6, 2).toHex()))
        .arg(QString(hash.mid(8, 2).toHex()))
        .arg(QString(hash.mid(10, 6).toHex()));

    return uuid;
}
