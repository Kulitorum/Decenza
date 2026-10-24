#pragma once

#include <QDate>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Pure CoffeeBag projections shared by the native storage-backed app and the
// Wasm client. This header deliberately does not expose CoffeeBagStorage or
// any QtSql types.
namespace CoffeeBagFields {

struct StorageHintOption { const char* value; const char* labelKey; const char* label; };
inline constexpr StorageHintOption kStorageHintOptions[] = {
    {"", "changebeans.form.storageHint.unset", "Not specified"},
    {"counter", "changebeans.form.storageHint.counter", "Counter"},
    {"airtight", "changebeans.form.storageHint.airtight", "Airtight container"},
    {"vacuum-sealed", "changebeans.form.storageHint.vacuum", "Vacuum-sealed"},
    {"fridge", "changebeans.form.storageHint.fridge", "Fridge"},
};

inline QVariantList storageHintOptions()
{
    QVariantList options;
    for (const auto& option : kStorageHintOptions)
        options << QVariantMap{{QStringLiteral("value"), QString::fromLatin1(option.value)},
                               {QStringLiteral("labelKey"), QString::fromLatin1(option.labelKey)},
                               {QStringLiteral("label"), QString::fromLatin1(option.label)}};
    return options;
}

int daysSince(const QString& iso, const QDate& today);
QVariantList lifecycleParts(const QVariantMap& bag, const QDate& today);
QVariantMap restockTemplate(QVariantMap bag, const QDate& today);
QVariantMap editChanges(const QVariantMap& opened, const QVariantMap& current,
                        bool replaceBlob);

} // namespace CoffeeBagFields
