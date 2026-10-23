#include "preparationstate.h"

#include <cmath>
#include <limits>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
bool readBool(const QJsonObject& object, const QString& key, bool* out)
{
    if (!object.value(key).isBool()) return false;
    *out = object.value(key).toBool();
    return true;
}
bool readNumber(const QJsonObject& object, const QString& key, double* out)
{
    const auto value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    *out = value.toDouble();
    return true;
}
bool readSequence(const QJsonObject& object, const QString& key, qint64* out)
{
    double value;
    if (!readNumber(object, key, &value) || value < 0 || value > 9007199254740991.0
        || std::trunc(value) != value) return false;
    *out = static_cast<qint64>(value);
    return true;
}
}

QJsonObject WeightCaptureSnapshot::toJson() const
{
    QJsonObject result;
#define INSERT_VALUE(name) result.insert(QStringLiteral(#name), name);
    DECENZA_CAPTURE_FIELDS(INSERT_VALUE, INSERT_VALUE)
#undef INSERT_VALUE
    return result;
}
bool WeightCaptureSnapshot::parse(const QJsonObject& object, WeightCaptureSnapshot* out, QString* error)
{
    WeightCaptureSnapshot state;
    if (!out || object.size() != state.toJson().size())
        return fail(error, QStringLiteral("Invalid capture snapshot fields"));
#define READ_BOOL(name) if (!readBool(object, QStringLiteral(#name), &state.name)) return fail(error, QStringLiteral("Invalid capture field " #name));
#define READ_DOUBLE(name) if (!readNumber(object, QStringLiteral(#name), &state.name)) return fail(error, QStringLiteral("Invalid capture field " #name));
    DECENZA_CAPTURE_FIELDS(READ_BOOL, READ_DOUBLE)
#undef READ_BOOL
#undef READ_DOUBLE
    if (state.cupWeight < 0 || state.minNet < 0 || state.maxNet < state.minNet)
        return fail(error, QStringLiteral("Invalid capture range"));
    *out = state;
    return true;
}

bool PreparationSnapshot::isMode(const QString& mode)
{
    return mode == QLatin1String("espresso") || mode == QLatin1String("steam")
        || mode == QLatin1String("hotwater") || mode == QLatin1String("flush");
}
QJsonObject PreparationSnapshot::toJson() const
{
    return {{QStringLiteral("mode"), mode},
            {QStringLiteral("milkTimeLocked"), milkTimeLocked},
            {QStringLiteral("milkAttemptOpen"), milkAttemptOpen},
            {QStringLiteral("sessionMeasuredMilkG"), sessionMeasuredMilkG},
            {QStringLiteral("lastOnScaleMilkG"), lastOnScaleMilkG},
            {QStringLiteral("steamTimeoutScaled"), steamTimeoutScaled},
            {QStringLiteral("lastBeanGrams"), lastBeanGrams},
            {QStringLiteral("lastMilkGrams"), lastMilkGrams},
            {QStringLiteral("lastMilkSeconds"), lastMilkSeconds},
            {QStringLiteral("beanCaptureSequence"), beanCaptureSequence},
            {QStringLiteral("milkCaptureSequence"), milkCaptureSequence},
            {QStringLiteral("bean"), bean.toJson()},
            {QStringLiteral("milk"), milk.toJson()}};
}
bool PreparationSnapshot::parse(const QJsonObject& object, PreparationSnapshot* out, QString* error)
{
    PreparationSnapshot state;
    qint64 seconds = 0;
    if (!out || object.size() != state.toJson().size()
        || !object.value(QStringLiteral("mode")).isString()
        || !isMode(object.value(QStringLiteral("mode")).toString())
        || !readBool(object, QStringLiteral("milkTimeLocked"), &state.milkTimeLocked)
        || !readBool(object, QStringLiteral("milkAttemptOpen"), &state.milkAttemptOpen)
        || !readNumber(object, QStringLiteral("sessionMeasuredMilkG"), &state.sessionMeasuredMilkG)
        || !readNumber(object, QStringLiteral("lastOnScaleMilkG"), &state.lastOnScaleMilkG)
        || !readBool(object, QStringLiteral("steamTimeoutScaled"), &state.steamTimeoutScaled)
        || !readNumber(object, QStringLiteral("lastBeanGrams"), &state.lastBeanGrams)
        || !readNumber(object, QStringLiteral("lastMilkGrams"), &state.lastMilkGrams)
        || !readSequence(object, QStringLiteral("beanCaptureSequence"), &state.beanCaptureSequence)
        || !readSequence(object, QStringLiteral("milkCaptureSequence"), &state.milkCaptureSequence)
        || !readSequence(object, QStringLiteral("lastMilkSeconds"), &seconds) || seconds > 120
        || state.sessionMeasuredMilkG < 0 || state.lastOnScaleMilkG < 0
        || state.lastBeanGrams < 0 || state.lastMilkGrams < 0)
        return fail(error, QStringLiteral("Invalid preparation snapshot"));
    if (!object.value(QStringLiteral("bean")).isObject()
        || !object.value(QStringLiteral("milk")).isObject()
        || !WeightCaptureSnapshot::parse(object.value(QStringLiteral("bean")).toObject(), &state.bean, error)
        || !WeightCaptureSnapshot::parse(object.value(QStringLiteral("milk")).toObject(), &state.milk, error))
        return fail(error, QStringLiteral("Invalid preparation capture snapshot"));
    state.mode = object.value(QStringLiteral("mode")).toString();
    state.lastMilkSeconds = static_cast<int>(seconds);
    *out = state;
    if (error) error->clear();
    return true;
}

void WeightCaptureState::apply(const WeightCaptureSnapshot& state)
{
#define SAME_VALUE(name) && m_state.name == state.name
    const bool same = true DECENZA_CAPTURE_FIELDS(SAME_VALUE, SAME_VALUE);
#undef SAME_VALUE
    if (same) return;
    m_state = state;
    emit changed();
}
PreparationState::PreparationState(QObject* parent) : QObject(parent) {}

double PreparationState::milkScaleLoadG() const
{
    const auto& milk = m_snapshot.milk;
    return milk.active && milk.seeded
        ? (milk.loadPresent ? milk.rawWeight - milk.virtualZero : 0)
        : std::numeric_limits<double>::quiet_NaN();
}
void PreparationState::applySnapshot(const PreparationSnapshot& state, bool available, bool deliverEvents)
{
    const bool beanEvent = deliverEvents && m_available && available
        && state.beanCaptureSequence > m_snapshot.beanCaptureSequence;
    const bool milkEvent = deliverEvents && m_available && available
        && state.milkCaptureSequence > m_snapshot.milkCaptureSequence;
    const bool changedMode = m_snapshot.mode != state.mode;
    const bool changedAvailability = m_available != available;
    m_snapshot = state;
    m_available = available;
    m_bean.apply(state.bean);
    m_milk.apply(state.milk);
    emit changed();
    if (changedMode) emit modeChanged();
    if (changedAvailability) emit availableChanged();
    if (beanEvent) emit beanCaptured(state.lastBeanGrams);
    if (milkEvent) emit milkCaptured(state.lastMilkGrams, state.lastMilkSeconds);
}
