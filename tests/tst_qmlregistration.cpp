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
#include <QDirIterator>
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

struct SingletonDecl {
    QString header;      // path, for failure messages
    QString cppName;     // the C++ class/struct carrying the macros
    QString registryName;// what Decenza.qmltypes keys the Component by
    QString qmlName;     // what QML actually types
    QString publishExpr; // expected text in main.cpp; empty = must NOT appear
};

// Where a QML_SINGLETON declaration was found but could not be parsed. Never silently dropped:
// a scan that quietly skips a class still returns a plausible count and passes every assertion,
// which is the same green-while-blind shape this whole file exists to catch.
struct ScanProblem { QString header; int line; QString why; };

// Every line that DECLARES the macro, independent of the parse below. The two counts are compared
// in the test, so a parse that drops one fails instead of checking N-1 of N.
int countSingletonDeclarations(const QStringList& lines)
{
    int n = 0;
    for (const QString& raw : lines) {
        QString ln = raw.section(QStringLiteral("//"), 0, 0).trimmed();
        if (ln == QStringLiteral("QML_SINGLETON"))
            ++n;
    }
    return n;
}

// src/main.cpp with // and /* */ comments blanked out. The publish assertions below are
// substring searches, and main.cpp documents these very calls BY NAME in its comments — so
// searching the raw text lets a commented-out publish line satisfy the positive check, and lets
// a comment like "EmojiAssets::setQmlInstance() is deliberately absent" fail the negative one.
// Both directions were wrong against the raw text; neither is against this.
QString sourceWithoutComments(const QString& src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false, inBlock = false, inStr = false;
    for (qsizetype i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar n = (i + 1 < src.size()) ? src.at(i + 1) : QChar();
        if (inLine) {
            if (c == QLatin1Char('\n')) { inLine = false; out.append(c); }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && n == QLatin1Char('/')) { inBlock = false; ++i; }
            else if (c == QLatin1Char('\n')) { out.append(c); }
            continue;
        }
        if (inStr) {
            if (c == QLatin1Char('\\')) { out.append(c); if (i + 1 < src.size()) out.append(src.at(++i)); continue; }
            if (c == QLatin1Char('"')) inStr = false;
            out.append(c);
            continue;
        }
        if (c == QLatin1Char('/') && n == QLatin1Char('/')) { inLine = true; ++i; continue; }
        if (c == QLatin1Char('/') && n == QLatin1Char('*')) { inBlock = true; ++i; continue; }
        if (c == QLatin1Char('"')) inStr = true;
        out.append(c);
    }
    return out;
}

QList<SingletonDecl> scanSingletons(QList<ScanProblem>* problems, int* declaredTotal)
{
    QList<SingletonDecl> out;
    static const QRegularExpression classRe(
        QStringLiteral("^\\s*(?:class|struct)\\s+(\\w+)"));
    static const QRegularExpression foreignRe(
        QStringLiteral("QML_FOREIGN\\(\\s*(\\w+)\\s*\\)"));
    static const QRegularExpression namedRe(
        QStringLiteral("QML_NAMED_ELEMENT\\(\\s*(\\w+)\\s*\\)"));

    QDirIterator it(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/src"),
                    {QStringLiteral("*.h")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString text = readOrEmpty(path);
        const QStringList lines = text.split(QLatin1Char('\n'));
        const QString rel = QString(path).remove(QStringLiteral(DECENZA_SOURCE_DIR) + QLatin1Char('/'));
        *declaredTotal += countSingletonDeclarations(lines);

        for (qsizetype i = 0; i < lines.size(); ++i) {
            // Strip a trailing // comment before matching, so `QML_SINGLETON  // engine-built`
            // is a declaration and a sentence merely mentioning the macro is not.
            if (lines.at(i).section(QStringLiteral("//"), 0, 0).trimmed()
                != QStringLiteral("QML_SINGLETON"))
                continue;

            SingletonDecl d;
            d.header = rel;

            // Nearest preceding class/struct is the one carrying the macro, and its body runs
            // until the NEXT class/struct declaration. Everything below is read from inside that
            // window, never from the whole file: settings_qml.h alone holds thirteen
            // QML_FOREIGN/QML_NAMED_ELEMENT pairs, and a file-wide match there resolves correctly
            // only by the accident that SettingsForeign happens to be declared first. Reorder
            // that header, or give any header a second QML_SINGLETON, and a file-wide match
            // silently validates the wrong type.
            qsizetype classAt = -1;
            for (qsizetype j = i; j >= 0; --j) {
                if (classRe.match(lines.at(j)).hasMatch()) { classAt = j; break; }
            }
            if (classAt < 0) {
                problems->append({rel, int(i) + 1,
                                  QStringLiteral("no enclosing class/struct declaration found")});
                continue;
            }
            d.cppName = classRe.match(lines.at(classAt)).captured(1);

            qsizetype bodyEnd = lines.size();
            for (qsizetype j = classAt + 1; j < lines.size(); ++j) {
                if (classRe.match(lines.at(j)).hasMatch()) { bodyEnd = j; break; }
            }
            const QString body = QStringList(lines.mid(classAt, bodyEnd - classAt))
                                     .join(QLatin1Char('\n'));

            // QML_FOREIGN retargets the registration at another type, and that foreign type is
            // what .qmltypes keys the Component by (Settings, not SettingsForeign).
            const auto fm = foreignRe.match(body);
            d.registryName = fm.hasMatch() ? fm.captured(1) : d.cppName;
            const auto nm = namedRe.match(body);
            d.qmlName = nm.hasMatch() ? nm.captured(1) : d.registryName;

            if (body.contains(QStringLiteral("static void setQmlInstance(")))
                d.publishExpr = d.cppName + QStringLiteral("::setQmlInstance(");
            else if (body.contains(QStringLiteral("s_singletonInstance")))
                d.publishExpr = d.cppName + QStringLiteral("::s_singletonInstance =");

            out.append(d);
        }
    }
    std::sort(out.begin(), out.end(), [](const SingletonDecl& a, const SingletonDecl& b) {
        return a.qmlName < b.qmlName;
    });
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

    // Every QML_SINGLETON in src/, DERIVED — not a hand-written list.
    //
    // A context property is invisible to qmllint, qmlcachegen and the language server; these
    // registrations are what make ~7,000 QML references checkable. But registration is only half
    // of it, and the two halves fail differently:
    //
    //   - The macros put the TYPE in Decenza.qmltypes.
    //   - A publish call in main.cpp supplies the INSTANCE the singleton resolves to, for the
    //     singletons that wrap an object main() already owns. Delete a publish line and the
    //     build is green, Decenza.qmltypes is still correct, qmllint is still green, and every
    //     binding through that singleton resolves to null at runtime.
    //
    // This used to be two tests over two hand-maintained row lists, which meant a future
    // singleton whose row nobody added was invisible to both. The distinguishing signal is
    // mechanical, so it is read from the headers instead: a class needs a publish call IFF it
    // declares `static void setQmlInstance(`. Engine-constructed singletons — stateless, no
    // create(), nothing for main() to hand over — declare no such thing, and are asserted to
    // have NO publish call, so converting one to the published shape without wiring main.cpp
    // fails here rather than in the field.
    //
    // Settings is the one irregular shape: QML_FOREIGN over a type it cannot annotate, so it
    // publishes by assigning SettingsForeign::s_singletonInstance rather than calling a setter.
    // That is detected from the header too, not exempted by name.
    void everyQmlSingletonIsRegisteredAndPublished_data()
    {
        QTest::addColumn<QString>("header");
        QTest::addColumn<QString>("cppName");
        QTest::addColumn<QString>("registryName");
        QTest::addColumn<QString>("qmlName");
        QTest::addColumn<QString>("publishExpr");
        QList<ScanProblem> problems;
        int declaredTotal = 0;
        const auto decls = scanSingletons(&problems, &declaredTotal);

        // Every declaration the scan could not parse, named. A floor like `>= 9` cannot catch a
        // TENTH singleton that the parser drops: the count stays at 9 and every row passes.
        for (const auto& p : problems)
            QFAIL(qPrintable(QStringLiteral("%1:%2 declares QML_SINGLETON but the scan could not "
                             "parse it (%3), so it was checked by nothing.")
                             .arg(p.header).arg(p.line, 0, 10).arg(p.why)));

        // Independent count vs parsed count. Not a floor — an equality, so a drop fails here
        // rather than silently checking N-1 of N.
        QCOMPARE(qsizetype(declaredTotal), decls.size());
        QVERIFY2(declaredTotal > 0,
                 "no QML_SINGLETON declarations found under src/; the scan is broken, not the code");
        for (const auto& d : decls)
            QTest::newRow(qPrintable(d.qmlName))
                << d.header << d.cppName << d.registryName << d.qmlName << d.publishExpr;
    }

    // A QML_SINGLETON that declares a create() factory must assert it is not
    // default-constructible, and the assert has to live beside the class.
    //
    // Qt picks the construction mode in singletonConstructionMode()
    // (qtdeclarative/src/qml/qml/qqmlprivate.h:155-167) and tests
    // is_default_constructible BEFORE it looks for the factory (:161-164). A
    // default-constructible singleton therefore gets `new T` at :190 and its
    // create() is never called at all — so main.cpp's published instance is
    // ignored and QML silently talks to an object Qt made instead.
    //
    // Decenza shipped that. AccessibilityManager's only constructor took
    // `QObject *parent = nullptr`, so every QML announcement reached Qt's orphan
    // while the MCP server and the coaching signal held main.cpp's object, each
    // with its own live QTextToSpeech. Nothing diagnosed it: the factory still
    // compiled and still looked wired.
    //
    // Asserted here rather than re-derived: this test cannot evaluate C++
    // default-constructibility, but the COMPILER can, and does, wherever the
    // static_assert is written. What this slot enforces is that a new singleton
    // cannot be added without one — which is the case no compile-time check can
    // cover, because the author who does not know the rule does not write the
    // assert. The five types that carry a factory besides AccessibilityManager
    // are safe only because their constructors happen to require arguments; the
    // asserts turn that coincidence into a stated invariant.
    void aSingletonWithAFactoryAssertsItIsNotDefaultConstructible_data()
    {
        QTest::addColumn<QString>("header");
        QTest::addColumn<QString>("cppName");
        QList<ScanProblem> problems;
        int declaredTotal = 0;
        const auto decls = scanSingletons(&problems, &declaredTotal);

        // Same blindness reconciliation the sibling slot does: a declaration the
        // scan dropped is checked by nothing, and would leave this green.
        for (const auto& p : problems)
            QFAIL(qPrintable(QStringLiteral("%1:%2 declares QML_SINGLETON but the scan could not "
                             "parse it (%3), so it was checked by nothing.")
                             .arg(p.header).arg(p.line, 0, 10).arg(p.why)));
        QCOMPARE(qsizetype(declaredTotal), decls.size());

        for (const auto& d : decls)
            QTest::newRow(qPrintable(d.qmlName)) << d.header << d.cppName;
    }

    void aSingletonWithAFactoryAssertsItIsNotDefaultConstructible()
    {
        QFETCH(QString, header);
        QFETCH(QString, cppName);

        // `header` is repo-relative (scanSingletons stores it that way for
        // failure messages), so resolve it the way every other slot here does.
        const QString src =
            readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + QLatin1Char('/') + header);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read %1").arg(header)));

        // Only types that declare their OWN factory are at risk. A singleton with
        // no create() is meant to be engine-constructed — that is the correct
        // Constructor mode — and a QML_FOREIGN wrapper takes Qt's FactoryWrapper
        // branch (:159-160) before the default-constructible test is ever reached.
        if (!src.contains(QStringLiteral("create(QQmlEngine"))
            || src.contains(QStringLiteral("QML_FOREIGN(")))
            QSKIP("no own create() factory, or QML_FOREIGN — not reachable by this defect");

        const QString expected =
            QStringLiteral("static_assert(!std::is_default_constructible_v<%1>").arg(cppName);
        QVERIFY2(src.contains(expected),
                 qPrintable(QStringLiteral(
                     "%1 declares create() but %2 has no "
                     "static_assert(!std::is_default_constructible_v<%2>). Qt tests "
                     "is_default_constructible BEFORE the factory (qqmlprivate.h:161-164), so if "
                     "this type is ever default-constructible Qt will 'new' its own instance and "
                     "never call create() — silently. Add the assert beside the class.")
                     .arg(header, cppName)));
    }

    void everyQmlSingletonIsRegisteredAndPublished()
    {
        QFETCH(QString, header);
        QFETCH(QString, cppName);
        QFETCH(QString, registryName);
        QFETCH(QString, qmlName);
        QFETCH(QString, publishExpr);

        const auto components = componentsByName(m_qmltypes);
        QVERIFY2(components.contains(registryName),
                 qPrintable(registryName + " (" + header + ") is absent from "
                            "Decenza.qmltypes. QML will resolve it to undefined at every call "
                            "site, and nothing else in the build will report it."));

        const QString& block = components.value(registryName);
        QVERIFY2(block.contains(QStringLiteral("isSingleton: true")),
                 qPrintable(registryName + " is in Decenza.qmltypes but not as a singleton, "
                            "so `" + qmlName + ".x` is a type reference rather than an "
                            "instance."));

        // The exported NAME, with its trailing space, not just the URI: a QML_NAMED_ELEMENT typo
        // compiles, registers, and exports under the wrong name, which reads as undefined at
        // every call site. The trailing space keeps the match anchored to a whole exported name
        // rather than a prefix of a longer one.
        QVERIFY2(block.contains(QStringLiteral("exports: [\"Decenza/") + qmlName
                                + QLatin1Char(' ')),
                 qPrintable(registryName + " is not exported as Decenza/" + qmlName
                            + ", so `" + qmlName + ".x` does not resolve. Exports line: "
                            + block.section(QStringLiteral("exports:"), 1, 1).section('\n', 0, 0)));

        const QString mainRaw = readOrEmpty(QStringLiteral(DECENZA_SOURCE_DIR) + "/src/main.cpp");
        QVERIFY2(!mainRaw.isEmpty(), "cannot read src/main.cpp");
        // Comments blanked: main.cpp names these calls in its own prose, so the raw text would
        // let a commented-out publish line pass the positive check below.
        const QString main = sourceWithoutComments(mainRaw);

        if (publishExpr.isEmpty()) {
            // Engine-constructed. If someone gives this class a create()/setQmlInstance() pair,
            // the branch above starts requiring the publish call and this assertion stops
            // applying — so the two directions cannot both be satisfied by accident.
            QVERIFY2(!main.contains(cppName + QStringLiteral("::setQmlInstance(")),
                     qPrintable(cppName + " declares no setQmlInstance() but main.cpp calls "
                                "one. Either the declaration was removed and the call left "
                                "behind, or this header and main.cpp disagree about which shape "
                                "this singleton is."));
            return;
        }

        const qsizetype publishAt = main.indexOf(publishExpr);
        QVERIFY2(publishAt >= 0,
                 qPrintable(QStringLiteral("main.cpp never has `%1`. The type is still registered, "
                            "so the build, qmllint and the rest of this file all stay green — and "
                            "every QML binding through %2 resolves to null.")
                            .arg(publishExpr, qmlName)));

        // Ordering is the one thing about create()'s null branch that is checkable from here.
        //
        // This compares TEXTUAL POSITION in main.cpp (QString::indexOf returns a UTF-16 code
        // unit index, not a byte offset — main.cpp has 265 non-ASCII lines), not execution order. It holds because main() is
        // straight-line and every publish call sits in one block above engine.load(). A publish
        // moved into a conditional or a lambda that appears earlier but runs later — or never —
        // would still pass. Tightening that needs the engine, not the text.
        const qsizetype loadAt = main.indexOf(QStringLiteral("engine.load("));
        QVERIFY2(loadAt >= 0, "cannot find engine.load() in main.cpp");
        QVERIFY2(publishAt < loadAt,
                 qPrintable(QStringLiteral("`%1` appears AFTER engine.load(). Any QML evaluated "
                            "during load resolves the singleton before the instance exists, and "
                            "create() hands back null.").arg(publishExpr)));
    }

    // Every MachineState.Phase.X that QML actually types must exist in the registered enum.
    //
    // This became load-bearing with this change. `MachineStateType` was a separate uncreatable
    // type, and Qt resolves enums on a non-singleton type name with no instance at all
    // (qqmltypewrapper.cpp, the `else` branch). MachineState is a singleton now, and there the
    // enum lookup sits INSIDE the `if (QObject *qobjectSingleton = ...)` guard — so all 153 of
    // these sites depend on the published instance in a way they previously did not.
    //
    // What that buys an attacker of this code: rename a Phase enumerator in machinestate.h,
    // leave QML naming the old one, and `phase === MachineState.Phase.Gone` is `undefined` on
    // the right-hand side. It does not throw. It is silently false, forever, on whichever
    // operation page used it. qmllint catches it only for files that are lintable and in the
    // baseline, which is not all of them.
    void qmlOnlyNamesPhaseEnumeratorsThatExist()
    {
        const QString block = componentsByName(m_qmltypes).value(QStringLiteral("MachineState"));
        QVERIFY2(!block.isEmpty(), "MachineState absent from Decenza.qmltypes");

        // The Enum block for Phase, then the names inside its `values:` list.
        const qsizetype phaseAt = block.indexOf(QStringLiteral("name: \"Phase\""));
        QVERIFY2(phaseAt >= 0, "MachineState has no Phase enum in Decenza.qmltypes");
        const QString valuesTail = block.mid(phaseAt);
        const qsizetype valuesAt = valuesTail.indexOf(QStringLiteral("values:"));
        QVERIFY2(valuesAt >= 0, "Phase enum has no values: list");

        // qmltyperegistrar writes `values: [ "Disconnected", "Sleep", ... ]` — a bare list, not
        // name:value pairs. Bound the scan at the closing bracket so a later enum in the same
        // Component cannot leak in.
        const qsizetype valuesEnd = valuesTail.indexOf(QLatin1Char(']'), valuesAt);
        QVERIFY2(valuesEnd > valuesAt, "Phase values: list is not terminated");
        QSet<QString> declared;
        static const QRegularExpression valueRe(QStringLiteral("\"(\\w+)\""));
        auto vit = valueRe.globalMatch(valuesTail.mid(valuesAt, valuesEnd - valuesAt));
        while (vit.hasNext())
            declared.insert(vit.next().captured(1));
        QVERIFY2(declared.size() >= 10,
                 qPrintable(QStringLiteral("parsed only %1 Phase enumerators from the qmltypes; "
                            "the parser is broken, not the code").arg(declared.size())));

        static const QRegularExpression useRe(
            QStringLiteral("MachineState\\.Phase\\.(\\w+)"));
        QSet<QString> used;
        QDirIterator it(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/qml"),
                        {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto m = useRe.globalMatch(readOrEmpty(it.next()));
            while (m.hasNext())
                used.insert(m.next().captured(1));
        }
        QVERIFY2(!used.isEmpty(), "found no MachineState.Phase.X uses in qml/; the scan is broken");

        const QSet<QString> unknown = used - declared;
        if (!unknown.isEmpty()) {
            QStringList names(unknown.cbegin(), unknown.cend());
            names.sort();
            QFAIL(qPrintable(QStringLiteral(
                "qml/ names Phase enumerators that do not exist: %1. Each one is `undefined` on "
                "the right of a comparison — silently false, no error, on whatever page uses it.")
                .arg(names.join(QStringLiteral(", ")))));
        }
    }

    // The same hazard as the Phase test above, for the UNSCOPED form: an unscoped Q_ENUM is
    // reachable straight off the singleton, so QML writes SteamHealthTracker.EstablishingAfterReset
    // with no enum name in the expression to anchor a targeted regex on. Generalised rather than
    // written for that one name, because the failure is silent and identical everywhere it occurs:
    // an enumerator that does not exist is `undefined`, and `state === undefined` is false rather
    // than an error, so the page renders its fallback wording forever and nothing is logged.
    //
    // This gap was found by review of the batch that created it. SteamHealthTracker's header had
    // carried a comment asserting that this file already pinned its exported name; it did not —
    // nothing under tests/ referenced SteamHealthTracker at all, so the rename that comment warned
    // against would have gone green. Deleting a false claim does not close the hole it described.
    void qmlOnlyNamesSingletonMembersThatExist()
    {
        // Key by EXPORT, not by `name:`. componentsByName() keys by the Component's own name,
        // which for a QML_FOREIGN registration is the C++ type (ScreensaverVideoManager) and not
        // what QML types (ScreensaverManager). The exports list carries the QML-visible name.
        QHash<QString, QString> byExport;
        static const QRegularExpression exportRe(QStringLiteral("\"Decenza/(\\w+) [0-9.]+\""));
        const QStringList blocks = m_qmltypes.split(QStringLiteral("    Component {"));
        for (const QString& b : blocks) {
            if (!b.contains(QStringLiteral("isSingleton: true")))
                continue;
            auto m = exportRe.globalMatch(b);
            while (m.hasNext())
                byExport.insert(m.next().captured(1), b);
        }
        QVERIFY2(byExport.size() >= 10,
                 qPrintable(QStringLiteral("found only %1 singleton exports in the qmltypes; the "
                            "parser is broken, not the code").arg(byExport.size())));

        // What a member access may legally resolve to: any declared name in the Component
        // (property, method, signal, or the enum's own name for the scoped form), plus every
        // enumerator inside a `values:` list.
        static const QRegularExpression nameRe(QStringLiteral("name: \"(\\w+)\""));
        static const QRegularExpression valuesRe(QStringLiteral("values: \\[([^\\]]*)\\]"));
        static const QRegularExpression quotedRe(QStringLiteral("\"(\\w+)\""));
        QHash<QString, QSet<QString>> allowed;
        for (auto kv = byExport.cbegin(); kv != byExport.cend(); ++kv) {
            QSet<QString> names;
            auto nm = nameRe.globalMatch(kv.value());
            while (nm.hasNext())
                names.insert(nm.next().captured(1));
            auto vm = valuesRe.globalMatch(kv.value());
            while (vm.hasNext()) {
                auto qm = quotedRe.globalMatch(vm.next().captured(1));
                while (qm.hasNext())
                    names.insert(qm.next().captured(1));
            }
            allowed.insert(kv.key(), names);
        }

        // Only uppercase-initial members are checked. Properties and methods are camelCase by
        // convention, so restricting to the initial capital keeps this to the enum-shaped accesses
        // this test is about, and keeps it from tripping over inherited QObject members that the
        // qmltypes does not re-declare on each Component.
        QStringList problems;
        int accessesChecked = 0;
        QDirIterator it(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/qml"),
                        {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString text = readOrEmpty(path);
            const QString rel = QString(path).remove(QStringLiteral(DECENZA_SOURCE_DIR)
                                                     + QLatin1Char('/'));
            for (auto kv = allowed.cbegin(); kv != allowed.cend(); ++kv) {
                const QString& qmlName = kv.key();
                if (!text.contains(qmlName))
                    continue;
                const QRegularExpression useRe(
                    QStringLiteral("\\b%1\\.([A-Z]\\w*)").arg(qmlName));
                auto um = useRe.globalMatch(text);
                while (um.hasNext()) {
                    const QString member = um.next().captured(1);
                    ++accessesChecked;
                    if (!kv.value().contains(member))
                        problems.append(QStringLiteral("%1: %2.%3").arg(rel, qmlName, member));
                }
            }
        }
        QVERIFY2(accessesChecked > 0,
                 "found no Singleton.UpperCaseMember accesses in qml/; the scan is broken");

        if (!problems.isEmpty()) {
            problems.sort();
            problems.removeDuplicates();
            QFAIL(qPrintable(QStringLiteral(
                "qml/ names singleton members that do not exist in Decenza.qmltypes:\n  %1\n"
                "Each is `undefined` at runtime. On the right of a comparison that is silently "
                "false — no error, no log, just the wrong branch taken forever.")
                .arg(problems.join(QStringLiteral("\n  ")))));
        }
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

        // There was a `deferredToOwnMigration` allowlist here holding ShotDataModel and
        // SteamDataModel, which were MainController property types still published as context
        // properties. Both are now QML_FOREIGN singletons (src/core/contextsingletons_qml.h), so
        // the allowlist is empty and is gone rather than left as an empty set: the assertion that
        // guarded it — fail the moment an entry becomes registered — is what forced this deletion,
        // and it did its job. Reintroduce the pair if a type ever needs deferring again.

        const auto components = componentsByName(m_qmltypes);
        QStringList missing;
        for (const QString& t : types) {
            // A name-prefix approximation of "Qt owns this type", NOT a real ownership test.
            // No project class currently starts with Q, so this skips nothing real today — but
            // a future Decenza `QrCodeGenerator` would be silently exempted from the very check
            // this test exists to provide. Narrow it to a known-Qt list if that day comes.
            if (t == QStringLiteral("QObject") || t.startsWith(QStringLiteral("Q")))
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
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral(
                     "these MainController property types are absent from Decenza.qmltypes: %1.\n"
                     "Add QML_ELEMENT + QML_UNCREATABLE to each class (and its directory to the "
                     "bare-basename include list in CMakeLists.txt). Without it, "
                     "MainController.<prop>.<member> is undefined at runtime and unverifiable by "
                     "every static tool.").arg(missing.join(QStringLiteral(", ")))));
    }

    // everySingletonInstanceIsPublished used to live here, over a hand-written list of the six
    // publish calls. everyQmlSingletonIsRegisteredAndPublished above subsumes it and derives the
    // same set from the headers, so a singleton added without a row is no longer invisible —
    // which was the whole gap. It also asserts the negative direction, which the hand list could
    // not: an engine-constructed singleton must have NO publish call.

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
