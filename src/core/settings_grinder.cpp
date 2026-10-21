#include "settings_grinder.h"
#include "grinderaliases.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

SettingsGrinder::SettingsGrinder(QObject* parent)
    : QObject(parent)
{
}

QString SettingsGrinder::recordKey(const QString& model, const QString& burrs)
{
    // Canonicalize the model the SAME way the consumer resolves a grinder:
    // through the alias registry, so "Niche" and "Niche Zero" land on one
    // record. Unknown grinders fall back to the trimmed raw string (the
    // consumer's kDefaultGrinder path has no canonical name to borrow).
    const GrinderAliases::GrinderEntry* e = GrinderAliases::findEntryByAlias(model);
    const QString m = e ? (e->brand + QStringLiteral(" ") + e->model).trimmed()
                        : model.trimmed();
    const QString b = burrs.trimmed();
    // '' (unit separator) joins the two fields: it cannot occur in a
    // grinder name, so "A" + "" cannot collide with "" + "A". empty↔empty is
    // the exact match a shot with no burrs string takes.
    return m + QChar(0x1f) + b;
}

QVariantMap SettingsGrinder::snapshotAll() const
{
    // Stored as a single JSON object under grinder/calibrations:
    //   { "<recordKey>": { conversionKey, ts, fineAnchor{...}, coarseAnchor{...} }, ... }
    const QString raw = m_settings.value(QStringLiteral("grinder/calibrations"), QString()).toString();
    if (raw.isEmpty()) return {};
    const QJsonObject obj = QJsonDocument::fromJson(raw.toUtf8()).object();
    return obj.toVariantMap();
}

static void writeStore(AppSettings& settings, const QVariantMap& store)
{
    const QJsonObject obj = QJsonObject::fromVariantMap(store);
    settings.setValue(QStringLiteral("grinder/calibrations"),
                      QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

QVariantMap SettingsGrinder::calibrationFor(const QString& model, const QString& burrs) const
{
    return calibrationFromSnapshot(snapshotAll(), model, burrs);
}

QVariantMap SettingsGrinder::calibrationFromSnapshot(const QVariantMap& snapshot,
                                                     const QString& model, const QString& burrs)
{
    const QString key = recordKey(model, burrs);
    const QVariantMap rec = snapshot.value(key).toMap();
    if (rec.isEmpty()) return {};

    const QVariantMap fine = rec.value(QStringLiteral("fineAnchor")).toMap();
    const QVariantMap coarse = rec.value(QStringLiteral("coarseAnchor")).toMap();
    if (fine.isEmpty() || coarse.isEmpty()) return {};  // only one anchor so far

    bool fOk = false, cOk = false, kOk = false;
    const double fUgs = fine.value(QStringLiteral("ugs")).toDouble(&fOk);
    const double cUgs = coarse.value(QStringLiteral("ugs")).toDouble(&cOk);
    const double key_ = rec.value(QStringLiteral("conversionKey")).toDouble(&kOk);
    // Re-validate on read — the JSON is user-editable (M6). Reject non-finite
    // values, a span below K, and a near-zero key. Sign-agnostic: a negative
    // key is legitimate (a grinder whose dial number falls as grind coarsens).
    if (!fOk || !cOk || !kOk) return {};
    if (!std::isfinite(fUgs) || !std::isfinite(cUgs) || !std::isfinite(key_)) return {};
    if (std::abs(cUgs - fUgs) < kMinAnchorSpanUgs) return {};
    if (std::abs(key_) <= 1e-9) return {};
    return rec;
}

bool SettingsGrinder::setAnchor(const QString& model, const QString& burrs,
                                const QString& which,
                                double setting, double ugs, double rpm)
{
    const QString side = (which.trimmed().compare(QStringLiteral("coarse"), Qt::CaseInsensitive) == 0)
        ? QStringLiteral("coarseAnchor") : QStringLiteral("fineAnchor");

    const QString key = recordKey(model, burrs);
    QVariantMap store = snapshotAll();
    QVariantMap rec = store.value(key).toMap();

    QVariantMap anchor;
    anchor[QStringLiteral("setting")] = setting;
    anchor[QStringLiteral("ugs")] = ugs;
    anchor[QStringLiteral("rpm")] = rpm;  // M4: recorded now, not acted on yet
    rec[side] = anchor;
    rec[QStringLiteral("ts")] = QDateTime::currentSecsSinceEpoch();

    // Compute the key only once both anchors exist AND the span clears K.
    const QVariantMap fine = rec.value(QStringLiteral("fineAnchor")).toMap();
    const QVariantMap coarse = rec.value(QStringLiteral("coarseAnchor")).toMap();
    if (!fine.isEmpty() && !coarse.isEmpty()) {
        const double fUgs = fine.value(QStringLiteral("ugs")).toDouble();
        const double cUgs = coarse.value(QStringLiteral("ugs")).toDouble();
        const double fSet = fine.value(QStringLiteral("setting")).toDouble();
        const double cSet = coarse.value(QStringLiteral("setting")).toDouble();
        if (std::abs(cUgs - fUgs) < kMinAnchorSpanUgs) {
            // Anchors too close — store the anchor but leave the record
            // unpublishable (no key). The user must re-anchor wider.
            rec.remove(QStringLiteral("conversionKey"));
        } else {
            rec[QStringLiteral("conversionKey")] = (cSet - fSet) / (cUgs - fUgs);
        }
    }

    store[key] = rec;
    writeStore(m_settings, store);
    emit calibrationChanged(model, burrs);
    return true;
}

void SettingsGrinder::clearCalibration(const QString& model, const QString& burrs)
{
    const QString key = recordKey(model, burrs);
    QVariantMap store = snapshotAll();
    if (!store.contains(key)) return;
    store.remove(key);
    writeStore(m_settings, store);
    emit calibrationChanged(model, burrs);
}
