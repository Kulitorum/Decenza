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
#include <QRegularExpression>
#include <QVariantMap>

#include <cmath>

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

// The local writes that bring a Visualizer bag's fields (GET
// /api/coffee_bags/:id) back to Decenza, as CoffeeBag variant-map keys. None for
// a bag with an unsent local edit (visualizerSyncPending).
//  - Freezer: a frozen_date that differs from local is taken with its
//    defrosted_date (Visualizer's Freeze clears the defrost date); otherwise a
//    differing defrosted_date is taken. Never cleared from a null.
//  - Descriptive fields fill local blanks only, never overwrite. The canonical
//    link is not filled: it carries the borrowed-record rules in BeanBaseBlob,
//    which a bare id would bypass.
inline QVariantMap bagFieldPullChanges(const QJsonObject& remote, const QVariantMap& local)
{
    QVariantMap changes;
    if (local.value(QStringLiteral("visualizerSyncPending")).toBool())
        return changes;
    auto remoteText = [&](const char* apiKey) {
        return remote.value(QLatin1StringView(apiKey)).toString().trimmed();
    };

    const QString frozen = remoteText("frozen_date");
    const QString defrosted = remoteText("defrosted_date");
    if (!frozen.isEmpty() && frozen != local.value(QStringLiteral("frozenDate")).toString()) {
        changes.insert(QStringLiteral("frozenDate"), frozen);
        if (defrosted != local.value(QStringLiteral("defrostDate")).toString())
            changes.insert(QStringLiteral("defrostDate"), defrosted);
    } else if (!defrosted.isEmpty() && defrosted != local.value(QStringLiteral("defrostDate")).toString()) {
        changes.insert(QStringLiteral("defrostDate"), defrosted);
    }

    auto fillBlank = [&](const char* key, const QString& value) {
        if (!value.isEmpty() && local.value(QLatin1StringView(key)).toString().trimmed().isEmpty())
            changes.insert(QLatin1StringView(key), value);
    };
    fillBlank("roastDate", remoteText("roast_date"));
    fillBlank("roastLevel", remoteText("roast_level"));
    fillBlank("notes", VisualizerNotes::htmlToPlain(remoteText("notes")));

    QJsonObject blob = QJsonDocument::fromJson(
        local.value(QStringLiteral("beanBaseData")).toString().toUtf8()).object();
    bool blobChanged = false;
    for (const BagBlobField& f : kBagBlobFields) {
        const QString value = remoteText(f.apiKey);
        const QString blobKey = QString::fromLatin1(f.blobKey);
        if (!value.isEmpty() && blob.value(blobKey).toString().trimmed().isEmpty()) {
            blob.insert(blobKey, value);
            blobChanged = true;
        }
    }
    if (blobChanged)
        changes.insert(QStringLiteral("beanBaseData"),
                       QString::fromUtf8(QJsonDocument(blob).toJson(QJsonDocument::Compact)));
    return changes;
}

} // namespace VisualizerSync
