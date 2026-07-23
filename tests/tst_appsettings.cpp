#include <QtTest>
#include <QDirIterator>
#include <QSettings>

// Guards the single canonical settings store (see the settings-store-identity
// capability). Two distinct concerns live here:
//
//  1. The store IDENTITY assumption. AppSettings names the canonical store with
//     the explicit two-argument QSettings(organization, application) form rather
//     than the default constructor, because the default form resolves against
//     QCoreApplication state that main.cpp temporarily mutates during app-name
//     migration. That is only safe if both forms address the SAME backing file —
//     asserted below rather than assumed, because every other piece of this
//     consolidation is built on it.
//
//  2. That no call site constructs a settings handle any other way. A stray
//     explicit "DE1Qt" handle would read a store that no longer exists after
//     migration; a stray bare QSettings would silently escape test isolation.

// Mirrors main.cpp's application identity. Kept as literals (not read from
// QCoreApplication) so the test still fails if someone changes main.cpp without
// considering the store.
static constexpr auto kOrganizationName = "DecentEspresso";
static constexpr auto kOrganizationDomain = "decentespresso.com";
static constexpr auto kApplicationName = "Decenza";

class tst_AppSettings : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganizationName));
        QCoreApplication::setOrganizationDomain(QString::fromLatin1(kOrganizationDomain));
        QCoreApplication::setApplicationName(QString::fromLatin1(kApplicationName));
    }

    // The explicit form AppSettings uses must land on the same file the default
    // form does — otherwise switching to it would silently strand every setting
    // the app has ever written through a bare QSettings().
    void explicitIdentityMatchesDefaultConstructedStore()
    {
        const QSettings explicitForm(QString::fromLatin1(kOrganizationName),
                                     QString::fromLatin1(kApplicationName));
        const QSettings defaultForm;

        QCOMPARE(explicitForm.fileName(), defaultForm.fileName());
    }

    // Belt and braces on the platform naming rule: the canonical store must be
    // named after the app, not after anything else. Catches a Qt behaviour
    // change that kept the two forms equal but moved both.
    void canonicalStoreIsNamedAfterTheApp()
    {
        const QSettings canonical(QString::fromLatin1(kOrganizationName),
                                  QString::fromLatin1(kApplicationName));
        const QString path = canonical.fileName();

        QVERIFY2(path.contains(QString::fromLatin1(kApplicationName)),
                 qPrintable(QStringLiteral("canonical store path does not name the app: %1").arg(path)));
        QVERIFY2(!path.contains(QStringLiteral("DE1Qt")),
                 qPrintable(QStringLiteral("canonical store still resolves to the legacy name: %1").arg(path)));

#ifdef Q_OS_MACOS
        QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("com.decentespresso.Decenza.plist"));
#endif
    }

    // Scans the shipped source for settings handles built any way other than
    // through AppSettings. A stray explicit "DE1Qt" handle would address a store
    // that the migration has destroyed; a stray bare QSettings would escape test
    // isolation. Both were the norm before this consolidation, which is why this
    // is enforced rather than documented.
    void noSourceFileConstructsItsOwnSettingsHandle()
    {
        // Where a handle on the legacy store is legitimate: the migration that
        // drains it, and factoryReset() which must clear it. Prose mentioning
        // the old name is fine and deliberately not matched — the hazard is a
        // live handle, not a comment recording why the name is gone.
        static const QStringList allowedLegacyNaming = {
            QStringLiteral("core/settingsstoremigration.cpp"),
            QStringLiteral("core/settings.cpp"),
        };
        // main.cpp's app-name migration deliberately default-constructs QSettings
        // while it has applicationName reassigned — that is the mechanism, not a
        // mistake, and it addresses the OLD app name on purpose.
        static const QStringList allowedBareConstruction = {
            QStringLiteral("main.cpp"),
        };

        const QDir srcRoot(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/src"));
        QVERIFY(srcRoot.exists());

        static const QRegularExpression bareDeclaration(
            QStringLiteral(R"(^\s*(?:mutable\s+)?QSettings\s+\w+\s*;)"),
            QRegularExpression::MultilineOption);
        static const QRegularExpression bareTemporary(QStringLiteral(R"(QSettings\s*\(\s*\)\s*\.)"));
        // A constructed handle naming the legacy store, e.g.
        // QSettings legacy(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        static const QRegularExpression legacyHandle(QStringLiteral(R"(QSettings\s+\w+\s*\([^;]*DE1Qt)"));

        QStringList legacyOffenders;
        QStringList bareOffenders;

        QDirIterator it(srcRoot.path(), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString relative = srcRoot.relativeFilePath(path);

            QFile file(path);
            QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
            const QString content = QString::fromUtf8(file.readAll());

            if (legacyHandle.match(content).hasMatch() && !allowedLegacyNaming.contains(relative))
                legacyOffenders.append(relative);

            const bool constructsBare = bareDeclaration.match(content).hasMatch()
                                        || bareTemporary.match(content).hasMatch();
            if (constructsBare && !allowedBareConstruction.contains(relative))
                bareOffenders.append(relative);
        }

        QVERIFY2(legacyOffenders.isEmpty(),
                 qPrintable(QStringLiteral("these name the legacy DE1Qt store, which no longer "
                                           "exists after migration: %1")
                                .arg(legacyOffenders.join(QStringLiteral(", ")))));
        QVERIFY2(bareOffenders.isEmpty(),
                 qPrintable(QStringLiteral("these construct a bare QSettings, escaping test "
                                           "isolation — use AppSettings: %1")
                                .arg(bareOffenders.join(QStringLiteral(", ")))));
    }
};

QTEST_MAIN(tst_AppSettings)
#include "tst_appsettings.moc"
