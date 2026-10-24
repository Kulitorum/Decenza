#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace SteamHealthSnapshot {

inline constexpr qint64 MaximumExactJsonInteger = 9007199254740991LL;
inline constexpr qsizetype MaximumEvents = 32;
inline constexpr qsizetype MaximumEventLedgerBytes = 8 * 1024;
inline constexpr qsizetype MaximumActiveWarnings = 2;
inline constexpr qsizetype MaximumMessageLength = 512;

enum class Kind {
    PressureTooHigh,
    TemperatureTooHigh,
    Descale,
    TemperatureWarning,
    ScaleBuildup,
};

QString kindName(Kind kind);
bool kindFromName(const QString& name, Kind* kind);
bool isLiveKind(Kind kind);

struct Warning {
    qint64 id = 0;
    QString sessionId;
    Kind kind = Kind::PressureTooHigh;
    QString message;
};

struct Snapshot {
    QString sessionId;
    bool sessionActive = false;
    qint64 oldestEventId = 0;
    qint64 latestEventId = 0;
    QList<Warning> activeWarnings;
    QList<Warning> events;

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& object, Snapshot* snapshot,
                      QString* error = nullptr);
};

qsizetype encodedEventArrayBytes(const QList<Warning>& events);
bool appendBoundedEvent(Snapshot* snapshot, const Warning& warning);

} // namespace SteamHealthSnapshot
