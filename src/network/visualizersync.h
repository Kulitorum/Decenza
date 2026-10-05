#pragma once

#include "roastdate.h"
#include "tastecvamap.h"
#include "visualizernotes.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QAnyStringView>
#include <QDateTime>
#include <QLatin1StringView>
#include <QMap>
#include <QRegularExpression>
#include <QVariantMap>

#include <cmath>
#include <functional>

// Two-way sync of a shot's editable fields with visualizer.coffee, and of a
// coffee bag's state back from it. Pure: no I/O, so the rules that decide what
// is written to either side are unit-tested directly (tst_visualizershotparse,
// tst_coffeebags).
namespace VisualizerSync {

// One bit per field Decenza can send in a shot PATCH. A local edit that changes
// a field's column sets its bit in shots.visualizer_dirty; an automatic update
// sends only the dirty fields, so it cannot overwrite an edit made on
// visualizer.coffee to anything else.
enum Field : quint32 {
    BeanBrand     = 1u << 0,
    BeanType      = 1u << 1,
    RoastDate     = 1u << 2,
    RoastLevel    = 1u << 3,
    GrinderModel  = 1u << 4,
    GrinderSetting = 1u << 5,  // grinder_setting with the rpm suffix
    DoseWeight    = 1u << 6,
    FinalWeight   = 1u << 7,
    DrinkTds      = 1u << 8,
    DrinkEy       = 1u << 9,
    Enjoyment     = 1u << 10,
    EspressoNotes = 1u << 11,
    Barista       = 1u << 12,
    Taste         = 1u << 13,  // acidity/bitterness/mouthfeel
    CanonicalBean = 1u << 14,
    ProfileTitle  = 1u << 15,  // no local edit path; sent only in a full update
};
constexpr quint32 kAllFields = (1u << 16) - 1;

// A shots column, the metadata key updateShotMetadataStatic takes for it, the
// field it feeds, and whether unset is 0 (numeric) or "" (text) — NULL is unset
// in both, so a form re-saving an empty field does not mark it edited.
struct Column {
    const char* column;
    const char* key;
    quint32 field;
    bool numeric;
};
inline constexpr Column kColumns[] = {
    {"bean_brand",      "beanBrand",      BeanBrand,      false},
    {"bean_type",       "beanType",       BeanType,       false},
    {"roast_date",      "roastDate",      RoastDate,      false},
    {"roast_level",     "roastLevel",     RoastLevel,     false},
    {"equipment_id",    "equipmentId",    GrinderModel,   true},
    {"grinder_setting", "grinderSetting", GrinderSetting, false},
    {"rpm",             "rpm",            GrinderSetting, true},
    {"dose_weight",     "doseWeight",     DoseWeight,     true},
    {"final_weight",    "finalWeight",    FinalWeight,    true},
    {"drink_tds",       "drinkTds",       DrinkTds,       true},
    {"drink_ey",        "drinkEy",        DrinkEy,        true},
    {"enjoyment",       "enjoyment",      Enjoyment,      true},
    {"espresso_notes",  "espressoNotes",  EspressoNotes,  false},
    {"barista",         "barista",        Barista,        false},
    {"taste_balance",   "tasteBalance",   Taste,          false},
    {"taste_body",      "tasteBody",      Taste,          false},
    {"beanbase_json",   "beanBaseJson",   CanonicalBean,  false},
};

inline const Column* columnNamed(QAnyStringView column)
{
    for (const Column& c : kColumns)
        if (QAnyStringView::equal(column, QLatin1StringView(c.column)))
            return &c;
    return nullptr;
}

inline const Column* columnForKey(QAnyStringView key)
{
    for (const Column& c : kColumns)
        if (QAnyStringView::equal(key, QLatin1StringView(c.key)))
            return &c;
    return nullptr;
}

// Visualizer has no rpm field, so the rpm rides on the grind setting using the
// community convention ("2.4 1400rpm") that the grinder parser already tolerates.
inline QString grinderSettingWithRpm(const QString& setting, qint64 rpm)
{
    if (rpm <= 0)
        return setting;
    return setting.isEmpty() ? QStringLiteral("%1rpm").arg(rpm)
                             : QStringLiteral("%1 %2rpm").arg(setting).arg(rpm);
}

inline double jsonNumber(const QJsonValue& v)
{
    // bean_weight, drink_weight, drink_tds and drink_ey are strings in the API.
    return v.isString() ? v.toString().toDouble() : v.toDouble();
}

// The values a Visualizer shot (GET /api/shots/:id) holds for the fields Decenza
// pulls, as updateShotMetadataStatic keys. A null or empty remote value is
// absent: pulling never clears a local value. A field Visualizer is missing was
// often never sent there (CVA scores need Premium; barista never reached it
// before the my_name fix), so absence cannot be read as "cleared".
// Grinder identity and the canonical bean link are not pulled: locally they are
// an equipment package and a Bean Base snapshot, not text.
inline QVariantMap remoteShotValues(const QJsonObject& remote)
{
    QVariantMap values;
    auto setText = [&](const char* apiKey, const char* key) {
        const QString s = remote.value(QLatin1StringView(apiKey)).toString().trimmed();
        if (!s.isEmpty())
            values.insert(QLatin1StringView(key), s);
    };
    auto setNumber = [&](const char* apiKey, const char* key) {
        const double d = jsonNumber(remote.value(QLatin1StringView(apiKey)));
        if (d > 0)
            values.insert(QLatin1StringView(key), d);
    };
    setText("bean_brand", "beanBrand");
    setText("bean_type", "beanType");
    setText("roast_level", "roastLevel");
    setText("barista", "barista");
    const QString roastDate = remote.value(QStringLiteral("roast_date")).toString().trimmed();
    if (!roastDate.isEmpty())
        values.insert(QStringLiteral("roastDate"), RoastDate::toIso(roastDate));

    const QString grind = remote.value(QStringLiteral("grinder_setting")).toString().trimmed();
    if (!grind.isEmpty()) {
        static const QRegularExpression rpmSuffix(QStringLiteral("^(.*?)\\s*(\\d+)\\s*rpm$"),
                                                  QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = rpmSuffix.match(grind);
        const QString setting = m.hasMatch() ? m.captured(1).trimmed() : grind;
        if (!setting.isEmpty())
            values.insert(QStringLiteral("grinderSetting"), setting);
        if (m.hasMatch())
            values.insert(QStringLiteral("rpm"), m.captured(2).toLongLong());
    }

    setNumber("bean_weight", "doseWeight");
    setNumber("drink_weight", "finalWeight");
    setNumber("drink_tds", "drinkTds");
    setNumber("drink_ey", "drinkEy");
    const int enjoyment = qRound(jsonNumber(remote.value(QStringLiteral("espresso_enjoyment"))));
    if (enjoyment > 0)
        values.insert(QStringLiteral("enjoyment"), enjoyment);

    const QString notes = VisualizerNotes::htmlToPlain(remote.value(QStringLiteral("espresso_notes")).toString());
    if (!notes.trimmed().isEmpty())
        values.insert(QStringLiteral("espressoNotes"), notes);

    const QString balance = cvaToTasteBalance(remote.value(QStringLiteral("acidity")).toInt(),
                                              remote.value(QStringLiteral("bitterness")).toInt());
    if (!balance.isEmpty())
        values.insert(QStringLiteral("tasteBalance"), balance);
    const QString body = cvaToTasteBody(remote.value(QStringLiteral("mouthfeel")).toInt());
    if (!body.isEmpty())
        values.insert(QStringLiteral("tasteBody"), body);
    return values;
}

// The local metadata writes a pull makes: each remote value that differs from
// the local one, unless the local field is dirty (edited here and not yet sent,
// so the local edit wins and goes out on the next update).
inline QVariantMap shotPullChanges(const QVariantMap& remote, const QVariantMap& local, quint32 dirty)
{
    QVariantMap changes;
    for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
        const Column* column = columnForKey(it.key());
        if (!column || (dirty & column->field))
            continue;
        const QVariant& r = it.value();
        const QVariant l = local.value(it.key());
        bool same;
        if (it.key() == QLatin1StringView("espressoNotes"))
            same = VisualizerNotes::sameNotes(r.toString(), l.toString());
        else if (it.key() == QLatin1StringView("roastDate"))
            same = RoastDate::toIso(l.toString()) == r.toString();
        else if (r.typeId() == QMetaType::Double)
            same = std::abs(r.toDouble() - l.toDouble()) < 1e-6;
        else if (r.typeId() == QMetaType::Int || r.typeId() == QMetaType::LongLong)
            same = r.toLongLong() == l.toLongLong();
        else
            same = r.toString() == l.toString().trimmed();
        if (!same)
            changes.insert(it.key(), r);
    }
    return changes;
}

// --- Coffee bags ---------------------------------------------------------

// Visualizer bag attribute → key in the local bag's beanBaseData blob.
struct BagBlobField {
    const char* apiKey;
    const char* blobKey;
};
inline constexpr BagBlobField kBagBlobFields[] = {
    {"country", "origin"},           {"region", "region"},
    {"farm", "farm"},                {"farmer", "producer"},
    {"variety", "variety"},          {"processing", "process"},
    {"harvest_time", "harvest"},     {"quality_score", "qualityScore"},
    {"place_of_purchase", "placeOfPurchase"},
    {"tasting_notes", "tastingNotes"}, {"elevation", "elevation"},
    {"url", "link"},
};

// Archive state is synced both ways, keyed on visualizerArchivedAt: the
// archived_at Visualizer was last known to hold.
//
// Pull: when Visualizer's archived_at differs from that, it changed there — an
// archive finishes the bag here, a restore returns it to inventory — and the
// new value is recorded. Acting on the change rather than the state is what
// stops a remote state Decenza has not caught up with from undoing a local one.
inline QVariantMap bagArchivePullChanges(const QString& remoteArchivedAt, const QVariantMap& local)
{
    QVariantMap changes;
    if (remoteArchivedAt == local.value(QStringLiteral("visualizerArchivedAt")).toString())
        return changes;
    changes.insert(QStringLiteral("visualizerArchivedAt"), remoteArchivedAt);
    const bool inInventory = local.value(QStringLiteral("inInventory")).toBool();
    if (!remoteArchivedAt.isEmpty() && inInventory)
        changes.insert(QStringLiteral("inInventory"), false);
    else if (remoteArchivedAt.isEmpty() && !inInventory)
        changes.insert(QStringLiteral("inInventory"), true);
    return changes;
}

// Push: the archived_at a bag PATCH carries when the bag's inventory state here
// disagrees with what Visualizer was last known to hold — finished here, active
// there: `now`; back in inventory here, archived there: null. False when they
// agree, so an edit to anything else never moves an archive it has not seen.
inline bool bagArchiveForPush(const QVariantMap& bag, const QDateTime& now, QJsonValue* out)
{
    const bool archivedThere = !bag.value(QStringLiteral("visualizerArchivedAt")).toString().isEmpty();
    const bool finishedHere = !bag.value(QStringLiteral("inInventory"), true).toBool();
    if (archivedThere == finishedHere)
        return false;
    *out = finishedHere ? QJsonValue(now.toUTC().toString(Qt::ISODate)) : QJsonValue(QJsonValue::Null);
    return true;
}

// The bag attributes synced both ways, as plain text keyed by API name ("" =
// unset). Notes are plain here; each route converts them (visualizernotes.h).
// Name and the canonical link are pushed but never pulled: locally they are the
// bag's identity and carry the borrowed-record rules in BeanBaseBlob.
inline QMap<QString, QString> bagLocalValues(const QVariantMap& bag)
{
    QMap<QString, QString> v;
    auto text = [&](const char* key) { return bag.value(QLatin1StringView(key)).toString().trimmed(); };
    v.insert(QStringLiteral("name"), text("coffeeName"));
    v.insert(QStringLiteral("roast_date"), text("roastDate"));
    v.insert(QStringLiteral("roast_level"), text("roastLevel"));
    v.insert(QStringLiteral("frozen_date"), text("frozenDate"));
    v.insert(QStringLiteral("defrosted_date"), text("defrostDate"));
    v.insert(QStringLiteral("notes"), bag.value(QStringLiteral("notes")).toString());
    v.insert(QStringLiteral("canonical_coffee_bag_id"), text("beanBaseId"));
    const QJsonObject blob = QJsonDocument::fromJson(bag.value(QStringLiteral("beanBaseData")).toString().toUtf8()).object();
    for (const BagBlobField& f : kBagBlobFields)
        v.insert(QString::fromLatin1(f.apiKey), blob.value(QLatin1StringView(f.blobKey)).toString().trimmed());
    return v;
}

inline QMap<QString, QString> bagRemoteValues(const QJsonObject& remote)
{
    QMap<QString, QString> v;
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it)
        v.insert(it.key(), it.value().toString().trimmed());
    v.insert(QStringLiteral("notes"), VisualizerNotes::htmlToPlain(remote.value(QStringLiteral("notes")).toString()));
    return v;
}

inline bool sameBagValue(const QString& key, const QString& a, const QString& b)
{
    return key == QLatin1StringView("notes") ? VisualizerNotes::sameNotes(a, b) : a == b;
}

inline QVariantMap bagSeen(const QVariantMap& bag)
{
    return QJsonDocument::fromJson(bag.value(QStringLiteral("visualizerSeen")).toString().toUtf8())
        .object().toVariantMap();
}

// Bag fields sync both ways off visualizerSeen: the value of each attribute
// Visualizer was last known to hold. A side "changed" a field when its value
// differs from that, so neither side's stale copy can overwrite the other's
// edit, and a clear on either side carries over like any other edit.

// Push: the PATCH body for the fields changed here since Visualizer was last
// seen — a clear goes as null. A field never seen (a bag synced before this
// existed) is sent only when set here, as before. `sent` gets the plain values
// sent, to record as seen once the server accepts them.
inline QJsonObject bagPushBody(const QVariantMap& bag, QVariantMap* sent)
{
    const QMap<QString, QString> local = bagLocalValues(bag);
    const QVariantMap seen = bagSeen(bag);
    QJsonObject body;
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        const QString& key = it.key();
        const QString& value = it.value();
        if (seen.contains(key) ? sameBagValue(key, value, seen.value(key).toString()) : value.isEmpty())
            continue;
        if (value.isEmpty() && key == QLatin1StringView("name"))
            continue;  // a bag must keep a name (the server 422s)
        if (value.isEmpty())
            body.insert(key, QJsonValue(QJsonValue::Null));
        else
            body.insert(key, key == QLatin1StringView("notes") ? VisualizerNotes::plainToHtml(value) : value);
        sent->insert(key, value);
    }
    return body;
}

// Pull: the local writes for the fields changed on Visualizer since last seen,
// as CoffeeBag variant-map keys, plus "visualizerSeen" — the seen values to
// record. A field changed on both sides keeps the local edit, which the next
// push sends. A field never seen only fills a local blank.
inline QVariantMap bagFieldPullChanges(const QJsonObject& remoteBag, const QVariantMap& bag)
{
    static const QList<QPair<QString, QString>> kPulled = {
        {QStringLiteral("roast_date"), QStringLiteral("roastDate")},
        {QStringLiteral("roast_level"), QStringLiteral("roastLevel")},
        {QStringLiteral("frozen_date"), QStringLiteral("frozenDate")},
        {QStringLiteral("defrosted_date"), QStringLiteral("defrostDate")},
        {QStringLiteral("notes"), QStringLiteral("notes")},
    };
    const QMap<QString, QString> remote = bagRemoteValues(remoteBag);
    const QMap<QString, QString> local = bagLocalValues(bag);
    const QVariantMap seen = bagSeen(bag);
    QVariantMap changes;
    QVariantMap seenUpdates;
    QJsonObject blob = QJsonDocument::fromJson(bag.value(QStringLiteral("beanBaseData")).toString().toUtf8()).object();
    bool blobChanged = false;

    auto consider = [&](const QString& apiKey, const std::function<void(const QString&)>& apply) {
        const QString r = remote.value(apiKey);
        const QString l = local.value(apiKey);
        if (seen.contains(apiKey)) {
            const QString s = seen.value(apiKey).toString();
            if (sameBagValue(apiKey, r, s))
                return;                       // unchanged there
            seenUpdates.insert(apiKey, r);
            if (sameBagValue(apiKey, l, s) && !sameBagValue(apiKey, l, r))
                apply(r);                     // changed there only
        } else {
            seenUpdates.insert(apiKey, r);
            if (l.isEmpty() && !r.isEmpty())
                apply(r);
        }
    };
    for (const auto& field : kPulled) {
        const QString localKey = field.second;
        consider(field.first, [&changes, localKey](const QString& r) { changes.insert(localKey, r); });
    }
    for (const BagBlobField& f : kBagBlobFields) {
        const QString blobKey = QString::fromLatin1(f.blobKey);
        consider(QString::fromLatin1(f.apiKey), [&, blobKey](const QString& r) {
            if (r.isEmpty())
                blob.remove(blobKey);
            else
                blob.insert(blobKey, r);
            blobChanged = true;
        });
    }
    if (blobChanged)
        changes.insert(QStringLiteral("beanBaseData"),
                       QString::fromUtf8(QJsonDocument(blob).toJson(QJsonDocument::Compact)));
    if (!seenUpdates.isEmpty())
        changes.insert(QStringLiteral("visualizerSeen"), seenUpdates);
    return changes;
}

} // namespace VisualizerSync
