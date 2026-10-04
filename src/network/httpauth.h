#pragma once

#include <QByteArray>
#include <QString>

// The HTTP Basic Authorization value every account call uses (Visualizer, the
// Decent account, the ShotServer's Visualizer test). The identity is trimmed;
// the secret is sent exactly as given, since spaces can be part of a password.
inline QByteArray basicAuthHeader(const QString& identity, const QString& secret) {
    return "Basic " + (identity.trimmed() + QLatin1Char(':') + secret).toUtf8().toBase64();
}
