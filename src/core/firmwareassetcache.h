#pragma once

#include <cstdint>
#include <optional>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include "core/firmwareheader.h"

// Downloads, caches, and validates the DE1 bootfwupdate.dat firmware
// binary from Decent's GitHub (main branch of decentespresso/de1app).
// A sidecar .meta.json file tracks the server ETag plus the version we
// parsed out of the header, so subsequent checks can answer "is there
// something new?" with a single HEAD + If-None-Match request.
//
// The pure helpers (MetaJson serialize/parse, rangeHeaderFor) are inline
// in this header and tested in tst_firmwareassetcachehelpers.cpp. The
// FirmwareAssetCache class itself is tested at the integration level
// (see tests/tst_firmwareflow.cpp).

namespace DE1::Firmware {

// Sidecar persistence record. Lives at cachedPath() + ".meta.json".
struct MetaJson {
    QString  etag;                 // server ETag of the last-observed remote file
    uint32_t version = 0;          // Version field parsed from the cached header
    qint64   downloadedAtEpoch = 0; // wall-clock seconds since epoch
};

// JSON encode / decode. parseMeta returns std::nullopt on malformed
// input or wrong field types (e.g. a string where a number is required).
inline QByteArray serializeMeta(const MetaJson& meta) {
    QJsonObject obj;
    obj.insert(QStringLiteral("etag"),              meta.etag);
    obj.insert(QStringLiteral("version"),           static_cast<qint64>(meta.version));
    obj.insert(QStringLiteral("downloadedAtEpoch"), meta.downloadedAtEpoch);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

inline std::optional<MetaJson> parseMeta(const QByteArray& json) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    QJsonObject obj = doc.object();

    // etag is optional (empty on first write before we've seen the server)
    // but must be a string when present.
    QJsonValue etagV = obj.value(QStringLiteral("etag"));
    if (!etagV.isUndefined() && !etagV.isNull() && !etagV.isString()) {
        return std::nullopt;
    }

    // version must be a number.
    QJsonValue versionV = obj.value(QStringLiteral("version"));
    if (!versionV.isDouble()) {
        return std::nullopt;
    }

    // downloadedAtEpoch must be a number.
    QJsonValue atV = obj.value(QStringLiteral("downloadedAtEpoch"));
    if (!atV.isDouble()) {
        return std::nullopt;
    }

    MetaJson meta;
    meta.etag              = etagV.isString() ? etagV.toString() : QString();
    meta.version           = static_cast<uint32_t>(versionV.toVariant().toLongLong());
    meta.downloadedAtEpoch = atV.toVariant().toLongLong();
    return meta;
}

// Compute the HTTP Range header to resume a partial download. Returns
// std::nullopt when a Range isn't appropriate (empty cache, full cache,
// or cache already larger than server's expected total — which means
// the cache is stale or corrupt and must be wiped).
//
// `expectedTotal < 0` means "server total unknown" — we still want to
// resume from whatever's already on disk.
inline std::optional<QByteArray> rangeHeaderFor(qint64 existingSize, qint64 expectedTotal) {
    if (existingSize <= 0) {
        return std::nullopt;
    }
    if (expectedTotal >= 0 && existingSize >= expectedTotal) {
        return std::nullopt;
    }
    return QByteArray("bytes=") + QByteArray::number(existingSize) + "-";
}

}  // namespace DE1::Firmware
