#include "../src/core/translationcatalog.h"

#include <QJsonObject>
#include <QtTest>

class TranslationCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void nativeLookupPreservesEmptyAndWhitespaceSemantics()
    {
        QCOMPARE(TranslationCatalog::resolve(QStringLiteral("Native fallback"), QString()),
                 QStringLiteral("Native fallback"));
        QCOMPARE(TranslationCatalog::resolve(QStringLiteral("Fallback"), QStringLiteral(" ")),
                 QStringLiteral(" "));
        QCOMPARE(TranslationCatalog::resolve(QStringLiteral("  fallback  "), QStringLiteral("  Übersetzt  ")),
                 QStringLiteral("  Übersetzt  "));
    }

    void snapshotIncludesRegistryAndTranslationUnionWithoutNormalizingText()
    {
        const QMap<QString, QString> registry{
            {QStringLiteral("with.translation"), QStringLiteral("  Source text  ")},
            {QStringLiteral("without.translation"), QStringLiteral("Untranslated")}
        };
        const QMap<QString, QString> translations{
            {QStringLiteral("with.translation"), QStringLiteral("  Translated  ")},
            {QStringLiteral("translation.only"), QStringLiteral("Direct translation")}
        };

        const auto exported = TranslationCatalog::exportSnapshot(QStringLiteral("fr"), 23, registry, translations);
        QVERIFY2(exported.isValid(), qPrintable(exported.error));
        QCOMPARE(exported.payload.value(QStringLiteral("language")).toString(), QStringLiteral("fr"));
        QCOMPARE(exported.payload.value(QStringLiteral("revision")).toDouble(), 23.0);

        const QJsonObject entries = exported.payload.value(QStringLiteral("entries")).toObject();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.value(QStringLiteral("with.translation")).toObject()
                     .value(QStringLiteral("fallback")).toString(), QStringLiteral("  Source text  "));
        QCOMPARE(entries.value(QStringLiteral("with.translation")).toObject()
                     .value(QStringLiteral("translation")).toString(), QStringLiteral("  Translated  "));
        QCOMPARE(entries.value(QStringLiteral("without.translation")).toObject()
                     .value(QStringLiteral("translation")).toString(), QString());
        QCOMPARE(entries.value(QStringLiteral("translation.only")).toObject()
                     .value(QStringLiteral("fallback")).toString(), QString());
        QCOMPARE(entries.value(QStringLiteral("translation.only")).toObject()
                     .value(QStringLiteral("translation")).toString(), QStringLiteral("Direct translation"));
    }

    void snapshotFailsClosedWhenAnEntryExceedsItsBound()
    {
        const QMap<QString, QString> registry{
            {QStringLiteral("oversized"), QString(16385, QLatin1Char('x'))}
        };
        const auto exported = TranslationCatalog::exportSnapshot(QStringLiteral("en"), 1, registry, {});
        QVERIFY(!exported.isValid());
        QVERIFY(exported.payload.isEmpty());
        QVERIFY(!exported.error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TranslationCatalogTest)
#include "tst_translationcatalog.moc"
