#include "shotfileparser.h"
#include <QFile>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QCryptographicHash>
#include <QDebug>

ShotFileParser::ParseResult ShotFileParser::parse(const QByteArray& fileContents, const QString& filename)
{
    ParseResult result;
    QString content = QString::fromUtf8(fileContents);

    // Extract timestamp
    QString clockStr = extractValue(content, "clock");
    if (clockStr.isEmpty()) {
        result.errorMessage = "Missing clock timestamp";
        return result;
    }

    qint64 timestamp = clockStr.toLongLong();
    if (timestamp == 0) {
        result.errorMessage = "Invalid clock timestamp";
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
    if (!waterDispensed.isEmpty())
        result.record.waterDispensed = toPointVector(elapsed, waterDispensed);
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
        result.record.grinderModel = settings.value("grinder_model").toString();
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
    int keyPos = content.indexOf(QRegularExpression(QString("^%1\\s+\\{").arg(QRegularExpression::escape(key)),
                                                     QRegularExpression::MultilineOption));
    if (keyPos < 0) return QString();

    // Find the opening brace
    int braceStart = content.indexOf('{', keyPos);
    if (braceStart < 0) return QString();

    // Find matching closing brace
    int braceCount = 1;
    int pos = braceStart + 1;
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
    int count = qMin(times.size(), values.size());
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
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

    int jsonStart = content.indexOf('{', profileStart);
    if (jsonStart < 0) return QString();

    // Find matching closing brace
    int braceCount = 1;
    int pos = jsonStart + 1;
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
