#include "settings_connections.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr auto kPairingsKey = "connections/scaleWifiPairings";

QVariantMap pairingEntry(const QString& ip, int port) {
    QVariantMap entry;
    entry.insert("ip", ip);
    entry.insert("port", port);
    entry.insert("lastSeenIso",
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return entry;
}

} // namespace

SettingsConnections::SettingsConnections(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
}

QVariantMap SettingsConnections::scaleWifiPairings() const {
    const QByteArray data = m_settings.value(kPairingsKey).toByteArray();
    if (data.isEmpty()) return {};
    return QJsonDocument::fromJson(data).object().toVariantMap();
}

void SettingsConnections::setScaleWifiPairings(const QVariantMap& pairings) {
    if (pairings == scaleWifiPairings()) return;
    const QJsonObject obj = QJsonObject::fromVariantMap(pairings);
    m_settings.setValue(kPairingsKey, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    emit scaleWifiPairingsChanged();
}

void SettingsConnections::setScaleWifiPairing(const QString& mac,
                                              const QString& ip,
                                              int port) {
    QVariantMap pairings = scaleWifiPairings();
    pairings.insert(mac.toLower(), pairingEntry(ip, port));
    setScaleWifiPairings(pairings);
}

void SettingsConnections::clearScaleWifiPairing(const QString& mac) {
    QVariantMap pairings = scaleWifiPairings();
    if (pairings.remove(mac.toLower()) > 0) {
        setScaleWifiPairings(pairings);
    }
}

QVariantMap SettingsConnections::scaleWifiPairing(const QString& mac) const {
    return scaleWifiPairings().value(mac.toLower()).toMap();
}

QString SettingsConnections::decenzaScaleLastCalibrationIso() const {
    return m_settings.value(QStringLiteral("connections/decenzaScaleLastCalibrationIso")).toString();
}

void SettingsConnections::setDecenzaScaleLastCalibrationIso(const QString& iso) {
    if (decenzaScaleLastCalibrationIso() == iso) return;
    m_settings.setValue(QStringLiteral("connections/decenzaScaleLastCalibrationIso"), iso);
    emit decenzaScaleLastCalibrationIsoChanged();
}
