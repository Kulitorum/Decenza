#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantMap>

// Per-device transport state that survives across launches: scale Wi-Fi
// pairings today, additional device-pairing identifiers (DE1 last-known-MAC,
// scale model preferences) in the future. See the
// `decenza-scale-connectivity` capability spec for the runtime path that
// consumes `scaleWifiPairings`.
class SettingsConnections : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantMap scaleWifiPairings READ scaleWifiPairings
                                              WRITE setScaleWifiPairings
                                              NOTIFY scaleWifiPairingsChanged)
    // ISO 8601 UTC timestamp of the last successful Decenza scale
    // calibration. Empty string means "never calibrated" — the UI shows
    // a prominent prompt in that case. Set by DecenzaCalibrationCard
    // after the post-calibration verification step lands within tolerance.
    Q_PROPERTY(QString decenzaScaleLastCalibrationIso READ decenzaScaleLastCalibrationIso
                                                       WRITE setDecenzaScaleLastCalibrationIso
                                                       NOTIFY decenzaScaleLastCalibrationIsoChanged)

public:
    explicit SettingsConnections(QObject* parent = nullptr);

    QVariantMap scaleWifiPairings() const;
    void setScaleWifiPairings(const QVariantMap& pairings);

    // Upsert a single pairing. The factory and provisioning UI both use this
    // rather than rewriting the whole map. `mac` is normalized to lowercase
    // so the key shape is stable regardless of caller.
    Q_INVOKABLE void setScaleWifiPairing(const QString& mac,
                                         const QString& ip,
                                         int port);

    // Remove a pairing. No-op if `mac` is not present.
    Q_INVOKABLE void clearScaleWifiPairing(const QString& mac);

    // Returns the entry as a QVariantMap with `ip`/`port`/`lastSeenIso` keys,
    // or an empty map if no pairing is stored. Empty-vs-missing is
    // indistinguishable to QML; callers check `.isEmpty()` or `ip` length.
    Q_INVOKABLE QVariantMap scaleWifiPairing(const QString& mac) const;

    QString decenzaScaleLastCalibrationIso() const;
    void setDecenzaScaleLastCalibrationIso(const QString& iso);

signals:
    void scaleWifiPairingsChanged();
    void decenzaScaleLastCalibrationIsoChanged();

private:
    mutable QSettings m_settings;
};
