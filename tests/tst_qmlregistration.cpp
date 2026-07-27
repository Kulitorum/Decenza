// Structural guard over the QML type registry.
//
// The failure this exists for is the quietest one in the codebase: a C++ type that QML is
// supposed to know about does not reach the module's type description, and NOTHING says so. The
// build is green, qmllint is green, the suite is green, and the app renders `undefined` on a
// screen nobody opened during review. It shipped that way once as #1661, and it happened twice
// more during the change that added this file:
//
//   - `Settings.<domain>.<prop>` was registered by runtime qmlRegisterUncreatableType<> calls in
//     main.cpp. qmltyperegistrar cannot see a runtime call, so the twelve domain types never
//     entered Decenza.qmltypes and no static tool knew they existed.
//   - Qt skips a module's declarative registration entirely if the module already exists
//     (qqmltypeloader.cpp: `if (!module) qmlRegisterModuleTypes(uri)`). Our remaining runtime
//     registrations create "Decenza" before QML first imports it, so every QML_ELEMENT type in
//     the module was silently absent at runtime — 1,081 ReferenceErrors, zero build output.
//     main.cpp works around it with an explicit qml_register_types_Decenza() call.
//
// Neither is reachable from a unit test the normal way: the registration is emitted by
// qmltyperegistrar on the Decenza QML module target, which the test binaries do not link. So
// this checks the two artifacts instead — the generated Decenza.qmltypes, and main.cpp's call.
// Weaker than executing it, and much stronger than nothing, which is what was there before.

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>

namespace {

QString readOrEmpty(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

// One `Component { ... }` block of a .qmltypes file, keyed by its `name:`.
QHash<QString, QString> componentsByName(const QString& qmltypes)
{
    QHash<QString, QString> out;
    // Blocks are separated by a `Component {` at a fixed indent; splitting on that is enough
    // for the presence checks below and avoids hand-rolling a QML parser.
    const QStringList blocks = qmltypes.split(QStringLiteral("    Component {"));
    static const QRegularExpression nameRe(QStringLiteral("\\n\\s+name: \"([^\"]+)\""));
    for (const QString& block : blocks) {
        const auto m = nameRe.match(block);
        if (m.hasMatch())
            out.insert(m.captured(1), block);
    }
    return out;
}

} // namespace

class tst_QmlRegistration : public QObject
{
    Q_OBJECT

    QString m_qmltypes;

private slots:
    void init() { QTest::failOnWarning(); }

    void initTestCase()
    {
        m_qmltypes = readOrEmpty(QStringLiteral(DECENZA_QMLTYPES_PATH));
        if (m_qmltypes.isEmpty()) {
            // Not silently skipped: name the file and the target, because a permanently-skipped
            // test is the same green-when-blind failure this file is about.
            QSKIP(qPrintable(
                QStringLiteral("Decenza.qmltypes not found at %1 — build the `Decenza` target "
                               "(not just the tests) for this check to run.")
                    .arg(QStringLiteral(DECENZA_QMLTYPES_PATH))));
        }
        QVERIFY2(m_qmltypes.contains(QStringLiteral("Component {")),
                 "Decenza.qmltypes has no Component blocks at all — the module registered "
                 "nothing, which is the whole failure this test exists to catch.");
    }

    // Everything that moved from setContextProperty() to singleton registration. A context
    // property is invisible to qmllint, qmlcachegen and the language server; these are the
    // registrations that make ~7,000 QML references checkable.
    //
    // Two shapes are in here and they fail differently. Most wrap an object main() already
    // owns, so they need BOTH the macros and a publish call — everySingletonInstanceIsPublished
    // below covers that second half. EmojiAssets, MarkdownRenderer and TemperatureDisplay are
    // stateless and engine-constructed, so they have no publish call and correctly have no row
    // there.
    //
    // Two names per row, because they are not always the same one and only one of them is what
    // QML actually types. A .qmltypes Component is keyed by the C++ class name; the QML name
    // lives in its `exports` line. TemperatureDisplayBridge exports as TemperatureDisplay, and
    // an earlier version of this test looked the QML name up as a class name and reported the
    // type as absent when it was registered correctly.
    void singletonsAreRegistered_data()
    {
        QTest::addColumn<QString>("className");
        QTest::addColumn<QString>("qmlName");
        QTest::newRow("Settings") << "Settings" << "Settings";
        QTest::newRow("TranslationManager") << "TranslationManager" << "TranslationManager";
        QTest::newRow("AccessibilityManager") << "AccessibilityManager" << "AccessibilityManager";
        QTest::newRow("MainController") << "MainController" << "MainController";
        QTest::newRow("ProfileManager") << "ProfileManager" << "ProfileManager";
        QTest::newRow("MachineState") << "MachineState" << "MachineState";
        QTest::newRow("EmojiAssets") << "EmojiAssets" << "EmojiAssets";
        QTest::newRow("MarkdownRenderer") << "MarkdownRenderer" << "MarkdownRenderer";
        QTest::newRow("TemperatureDisplay") << "TemperatureDisplayBridge" << "TemperatureDisplay";
    }

    void singletonsAreRegistered()
    {
        QFETCH(QString, className);
        QFETCH(QString, qmlName);
        const auto components = componentsByName(m_qmltypes);
        QVERIFY2(components.contains(className),
                 qPrintable(className + " is absent from Decenza.qmltypes. QML will resolve it to "
                                        "undefined at every call site, and nothing else in the "
                                        "build will report it."));
        const QString& block = components.value(className);
        QVERIFY2(block.contains(QStringLiteral("isSingleton: true")),
                 qPrintable(className + " is in Decenza.qmltypes but not as a singleton — "
                                        "QML_SINGLETON is missing, so `" + qmlName
                            + ".x` is a type reference rather than an instance."));
        // The exported NAME, not just the URI: a QML_NAMED_ELEMENT typo compiles, registers, and
        // exports under the wrong name, which reads as undefined at every call site.
        QVERIFY2(block.contains(QStringLiteral("exports: [\"Decenza/") + qmlName + QLatin1Char(' ')),
                 qPrintable(className + " is not exported as Decenza/" + qmlName
                            + ", so `" + qmlName + ".x` does not resolve. Exports line: "
                            + block.section(QStringLiteral("exports:"), 1, 1).section('\n', 0, 0)));
    }

    // Every domain sub-object declared on Settings must also be a QML-known type, or chained
    // access `Settings.<domain>.<prop>` resolves to undefined at runtime while compiling clean.
    // The expected set is derived from settings.h rather than hard-coded, so a thirteenth domain
    // added without its settings_qml.h gadget fails here instead of in the field.
    void everyDomainSubObjectIsRegistered()
    {
        const QString header =
            readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/core/settings.h");
        QVERIFY2(!header.isEmpty(), "cannot read src/core/settings.h");

        static const QRegularExpression propRe(
            QStringLiteral("Q_PROPERTY\\(\\s*(Settings\\w+)\\s*\\*"));
        QSet<QString> domains;
        auto it = propRe.globalMatch(header);
        while (it.hasNext())
            domains.insert(it.next().captured(1));

        QVERIFY2(domains.size() >= 12,
                 qPrintable(QStringLiteral("found only %1 domain Q_PROPERTYs in settings.h — the "
                                           "regex has drifted from the header, so this test is "
                                           "checking less than it claims")
                                .arg(domains.size())));

        const auto components = componentsByName(m_qmltypes);
        for (const QString& domain : domains) {
            QVERIFY2(components.contains(domain),
                     qPrintable(domain + " is a Settings domain but is absent from "
                                         "Decenza.qmltypes. Add a " + domain
                                + "Foreign gadget to src/core/settings_qml.h — written out "
                                  "literally, because moc does not expand a macro that declares "
                                  "a Q_GADGET and a generated one registers nothing."));
            QVERIFY2(components.value(domain).contains(
                         QStringLiteral("exports: [\"Decenza/%1Type").arg(domain)),
                     qPrintable(domain + " is registered but not exported as " + domain
                                + "Type under the Decenza URI."));
        }
    }

    // Same contract one level out: every type reachable as a MainController property must itself
    // be registered, or QML resolves MainController, follows the property, and finds nothing —
    // which qmllint reports as unresolved-type and the app renders as undefined. Registering
    // MainController alone took that category from 2 to 763; registering its 22 sub-object types
    // took it back to 2. Derived from the header so a 23rd added without QML_ELEMENT fails here.
    void everyMainControllerSubObjectIsRegistered()
    {
        const QString header =
            readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/controllers/maincontroller.h");
        QVERIFY2(!header.isEmpty(), "cannot read src/controllers/maincontroller.h");

        // Q_PROPERTY(Foo* bar READ ...) — the pointer-typed ones are the chainable surface.
        static const QRegularExpression propRe(
            QStringLiteral("Q_PROPERTY\\(\\s*(\\w+)\\s*\\*"));
        QSet<QString> types;
        auto it = propRe.globalMatch(header);
        while (it.hasNext())
            types.insert(it.next().captured(1));

        QVERIFY2(types.size() >= 15,
                 qPrintable(QStringLiteral("found only %1 pointer Q_PROPERTYs on MainController — "
                                           "the regex has drifted from the header, so this test is "
                                           "checking less than it claims").arg(types.size())));

        // Deferred to their own migration, with a reason and an expiry rather than a silent gap.
        // Both are ALSO published as context properties in main.cpp today, and registering them
        // early forces `#include "fastlinerenderer.h"` (for the Q_INVOKABLE registerFastSeries
        // parameter type) which pulls <QQuickItem> into every target that transitively includes
        // maincontroller.h — tst_mqttclient among them, and it does not link Qt6::Quick. Spreading
        // QtQuick across test targets to satisfy a registration that isn't due yet is the wrong
        // trade. Delete each entry when that name's own migration lands.
        static const QSet<QString> deferredToOwnMigration = {
            QStringLiteral("ShotDataModel"),   // tasks.md 3.x — still a context property
            QStringLiteral("SteamDataModel"),  // tasks.md 3.x — still a context property
        };

        const auto components = componentsByName(m_qmltypes);
        QStringList missing;
        for (const QString& t : types) {
            // A name-prefix approximation of "Qt owns this type", NOT a real ownership test.
            // No project class currently starts with Q, so this skips nothing real today — but
            // a future Decenza `QrCodeGenerator` would be silently exempted from the very check
            // this test exists to provide. Narrow it to a known-Qt list if that day comes.
            if (t == QStringLiteral("QObject") || t.startsWith(QStringLiteral("Q")))
                continue;
            if (deferredToOwnMigration.contains(t))
                continue;
            if (!components.contains(t)) {
                missing << t;
                continue;
            }
            // Presence is not enough. qmltyperegistrar emits un-exported Component blocks for
            // base classes (QAbstractItemModel, QAbstractListModel are both in there), so a type
            // that lost QML_ELEMENT can still appear as some sibling's prototype. QML only sees
            // what is EXPORTED under the module URI.
            if (!components.value(t).contains(QStringLiteral("exports: [\"Decenza/")))
                missing << (t + QStringLiteral(" (present but not exported under Decenza/)"));
        }
        // Assert the negative, so the exemption cannot outlive its reason: the day one of these
        // is registered, this fails and tells you to delete the entry. An allowlist nobody is
        // forced to revisit decays into a permanent blind spot.
        for (const QString& t : deferredToOwnMigration) {
            QVERIFY2(!components.contains(t),
                     qPrintable(t + QStringLiteral(" is now registered — delete its "
                                "deferredToOwnMigration entry in this file so it is checked "
                                "like every other MainController property type.")));
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral(
                     "these MainController property types are absent from Decenza.qmltypes: %1.\n"
                     "Add QML_ELEMENT + QML_UNCREATABLE to each class (and its directory to the "
                     "bare-basename include list in CMakeLists.txt). Without it, "
                     "MainController.<prop>.<member> is undefined at runtime and unverifiable by "
                     "every static tool.").arg(missing.join(QStringLiteral(", ")))));
    }

    // REGISTRATION AND PUBLICATION ARE TWO INDEPENDENT HALVES, and only one of them is visible
    // to any static tool. The macros put the TYPE in the registry; a setQmlInstance() call in
    // main.cpp supplies the INSTANCE the singleton resolves to. Delete a publish line and the
    // build is green, Decenza.qmltypes is still correct, qmllint is still green, and every
    // binding through that singleton resolves to null at runtime.
    //
    // This file asserted the registration half from the day it was written and left the
    // publication half on the honour system — found in review, by two reviewers independently.
    // The risk is not theoretical: these calls sit in the middle of ~40 setContextProperty lines
    // that this migration is deleting one at a time, so an odd-looking non-setContextProperty
    // line in that block is exactly what gets tidied away.
    void everySingletonInstanceIsPublished_data()
    {
        QTest::addColumn<QString>("publishCall");
        QTest::newRow("MainController")       << "MainController::setQmlInstance(";
        QTest::newRow("ProfileManager")       << "ProfileManager::setQmlInstance(";
        QTest::newRow("MachineState")         << "MachineState::setQmlInstance(";
        QTest::newRow("TranslationManager")   << "TranslationManager::setQmlInstance(";
        QTest::newRow("AccessibilityManager") << "AccessibilityManager::setQmlInstance(";
        QTest::newRow("Settings")             << "SettingsForeign::s_singletonInstance =";
    }

    void everySingletonInstanceIsPublished()
    {
        QFETCH(QString, publishCall);
        const QString main = readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/main.cpp");
        QVERIFY2(!main.isEmpty(), "cannot read src/main.cpp");

        const qsizetype publishAt = main.indexOf(publishCall);
        QVERIFY2(publishAt >= 0,
                 qPrintable(QStringLiteral("main.cpp never calls `%1`. The type is still registered, "
                            "so the build, qmllint and the rest of this file all stay green — and "
                            "every QML binding through that singleton resolves to null.")
                            .arg(publishCall)));

        // Ordering is the one thing about create()'s null branch that IS checkable from here:
        // publishing after the engine has loaded means QML may resolve the singleton first.
        const qsizetype loadAt = main.indexOf(QStringLiteral("engine.load("));
        QVERIFY2(loadAt >= 0, "cannot find engine.load() in main.cpp");
        QVERIFY2(publishAt < loadAt,
                 qPrintable(QStringLiteral("`%1` happens AFTER engine.load(). Any QML evaluated "
                            "during load resolves the singleton before the instance exists, and "
                            "create() hands back null.").arg(publishCall)));
    }

    // Qt registers a module's declarative types only `if (!module)` — see the file header. Our
    // runtime registrations create "Decenza" first, so without the explicit call NOTHING declared
    // with QML_ELEMENT in this module reaches the runtime registry. Both halves are asserted: the
    // call, and the condition that makes it necessary. If the runtime registrations ever go away,
    // this test says so rather than leaving a call nobody can explain.
    void mainCallsTheModuleRegistrationExplicitly()
    {
        const QString main = readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/main.cpp");
        QVERIFY2(!main.isEmpty(), "cannot read src/main.cpp");

        QVERIFY2(main.contains(QStringLiteral("qml_register_types_Decenza();")),
                 "main.cpp does not call qml_register_types_Decenza(). Qt skips a module's "
                 "declarative registration when the module already exists, and main.cpp's "
                 "runtime qmlRegister* calls create it first — so every QML_ELEMENT type in the "
                 "module would be silently absent at runtime.");

        static const QRegularExpression runtimeRe(
            QStringLiteral("qmlRegister\\w+\\s*<[^>]+>\\s*\\(\\s*\"Decenza\""));
        QVERIFY2(runtimeRe.match(main).hasMatch(),
                 "main.cpp no longer makes any runtime qmlRegister*(\"Decenza\", ...) call. That "
                 "is the condition that forces the explicit qml_register_types_Decenza() call "
                 "above. Re-read qqmltypeloader.cpp before removing either — and update this "
                 "test to match what is actually true.");
    }
};

QTEST_APPLESS_MAIN(tst_QmlRegistration)
#include "tst_qmlregistration.moc"
