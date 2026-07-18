// Spike + guard for the "invokable in a binding never re-evaluates" trap.
//
// A QML binding re-evaluates when a NOTIFY fires for a property it READ during its
// last evaluation. Calling a Q_INVOKABLE registers no such dependency, so
//
//     text: TranslationManager.translate("k", "f")
//
// computes once and then freezes — which is why a language change left 3,248 call
// sites showing the old language until restart.
//
// The proposed fix (design.md D1) is to expose `translate` as a PROPERTY whose value
// is a callable, so that reading it registers the dependency and the call site's
// syntax is unchanged. This file proves that claim before the codebase is swept, and
// then keeps proving it.
//
// Test A establishes the baseline failure (invokable => frozen). Test B is the claim.
// If B ever fails, the mechanism is gone and every translated string in the app is
// silently stale again.

#include <QtTest>
#include <QJSValue>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>

#include <QNetworkAccessManager>

#include "core/settings.h"
#include "core/translationmanager.h"

// Minimal stand-in for TranslationManager: one string that changes, exposed both ways.
class SpikeTranslator : public QObject {
    Q_OBJECT

    // The mechanism under test. QJSValue-valued, NOTIFY-backed: reading it is a
    // property read, so a binding that reads it depends on translationsChanged.
    Q_PROPERTY(QJSValue translate READ translateFn NOTIFY translationsChanged)

public:
    explicit SpikeTranslator(QJSEngine* engine, QObject* parent = nullptr)
        : QObject(parent), m_engine(engine) {}

    // The actual lookup. Also the C++ entry point — the real change keeps this
    // callable from C++ (10 call sites) regardless of the QML-facing shape.
    Q_INVOKABLE QString translateString(const QString& key, const QString& fallback) const {
        ++m_lookupCount;
        return m_translations.value(key, fallback);
    }

    QJSValue translateFn() {
        // Cache the callable: a binding that re-evaluates should not rebuild the
        // wrapper each time. Ownership stays with C++ — without this the JS engine
        // may collect the object out from under the callable.
        if (m_fn.isUndefined() || m_fn.isNull()) {
            QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
            QJSValue wrapper = m_engine->newQObject(this);
            m_fn = wrapper.property(QStringLiteral("translateString"));
        }
        return m_fn;
    }

    void setTranslation(const QString& key, const QString& value) {
        m_translations[key] = value;
        emit translationsChanged();
    }

    int lookupCount() const { return m_lookupCount; }
    void resetLookupCount() { m_lookupCount = 0; }

signals:
    void translationsChanged();

private:
    QJSEngine* m_engine;
    QJSValue m_fn;
    QMap<QString, QString> m_translations;
    mutable int m_lookupCount = 0;
};

class TestTranslationReactivity : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void invokableInBindingIsFrozen();
    void propertyReturningCallableReEvaluates();
    void callableIsCachedAcrossEvaluations();
    void callableWorksOutsideBindings();
    void realTranslateIsANotifyingProperty();
    void realTranslateFnIsCallableAndCached();
};

// BASELINE: documents the bug. If this ever starts passing, Qt changed its
// dependency tracking and the whole premise needs revisiting.
void TestTranslationReactivity::invokableInBindingIsFrozen()
{
    QQmlEngine engine;
    SpikeTranslator t(&engine);
    t.setTranslation("greeting", "Hello");
    engine.rootContext()->setContextProperty("T", &t);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        QtObject { property string label: T.translateString("greeting", "Hello") }
    )", QUrl("qrc:/spike_invokable.qml"));

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, qPrintable(component.errorString()));
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hello"));

    t.setTranslation("greeting", "Hallo");

    // The defect, demonstrated: notify fired, binding did not re-run.
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hello"));
}

// THE CLAIM (design.md D1): identical call syntax, but reactive.
void TestTranslationReactivity::propertyReturningCallableReEvaluates()
{
    QQmlEngine engine;
    SpikeTranslator t(&engine);
    t.setTranslation("greeting", "Hello");
    engine.rootContext()->setContextProperty("T", &t);

    QQmlComponent component(&engine);
    // Note: `T.translate(...)` — byte-for-byte what the 3,248 existing call sites
    // already write. That is the point: no call site changes.
    component.setData(R"(
        import QtQml
        QtObject { property string label: T.translate("greeting", "Hello") }
    )", QUrl("qrc:/spike_property.qml"));

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, qPrintable(component.errorString()));
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hello"));

    t.setTranslation("greeting", "Hallo");
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hallo"));

    // And back again — a switch away and back must leave no residue.
    t.setTranslation("greeting", "Hello");
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hello"));
}

// Cost check: re-evaluation must not rebuild the wrapper object every time.
void TestTranslationReactivity::callableIsCachedAcrossEvaluations()
{
    QQmlEngine engine;
    SpikeTranslator t(&engine);
    t.setTranslation("k", "v0");
    engine.rootContext()->setContextProperty("T", &t);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        QtObject { property string label: T.translate("k", "fallback") }
    )", QUrl("qrc:/spike_cache.qml"));

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, qPrintable(component.errorString()));

    const QJSValue first = t.translateFn();
    for (int i = 0; i < 20; ++i)
        t.setTranslation("k", QStringLiteral("v%1").arg(i));

    QCOMPARE(obj->property("label").toString(), QStringLiteral("v19"));
    QVERIFY(first.strictlyEquals(t.translateFn()));  // same function object
}

// A property whose value is a function must still work where there is no binding
// context at all — signal handlers, Component.onCompleted, imperative JS.
void TestTranslationReactivity::callableWorksOutsideBindings()
{
    QQmlEngine engine;
    SpikeTranslator t(&engine);
    t.setTranslation("greeting", "Hallo");
    engine.rootContext()->setContextProperty("T", &t);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        QtObject {
            property string label: ""
            Component.onCompleted: label = T.translate("greeting", "Hello")
        }
    )", QUrl("qrc:/spike_imperative.qml"));

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, qPrintable(component.errorString()));
    QCOMPARE(obj->property("label").toString(), QStringLiteral("Hallo"));
}

// THE GUARD (design.md D2). Everything above proves the MECHANISM works on a stand-in;
// this asserts the real TranslationManager actually uses it.
//
// Without this, someone refactoring `translate` back to a plain Q_INVOKABLE would break
// every translated string in the app — 3,248 bindings silently freezing on whatever
// language was active at construction — and no other test would notice, because each one
// still returns the right string when called directly. The defect only shows after a
// language change, which no unit test performs.
//
// Deliberately reflective rather than behavioural: constructing a TranslationManager needs
// a QNetworkAccessManager and Settings, and pulls in the whole translation-file stack. The
// property's SHAPE is what the 3,248 bindings depend on, and the shape is what this checks.
void TestTranslationReactivity::realTranslateIsANotifyingProperty()
{
    const QMetaObject* mo = &TranslationManager::staticMetaObject;

    const int idx = mo->indexOfProperty("translate");
    QVERIFY2(idx >= 0,
             "TranslationManager has no 'translate' PROPERTY. If it was changed back to a "
             "Q_INVOKABLE, every translated binding in the app has stopped updating on a "
             "language change. See translationmanager.h.");

    const QMetaProperty prop = mo->property(idx);
    QVERIFY2(prop.hasNotifySignal(),
             "'translate' must have a NOTIFY signal — without one a binding reading it never "
             "re-evaluates, which is the whole bug this exists to prevent.");
    QCOMPARE(QByteArray(prop.notifySignal().name()), QByteArray("translationsChanged"));

    // QJSValue is what lets the property hold a callable, so call sites keep the
    // translate("key", "fallback") syntax unchanged.
    QCOMPARE(QByteArray(prop.typeName()), QByteArray("QJSValue"));

    // And the C++ entry point must still exist for the 10 non-QML callers.
    QVERIFY2(mo->indexOfMethod("translateString(QString,QString)") >= 0,
             "translateString() is gone — C++ callers (updatechecker, blemanager, aimanager, "
             "aiconversation, livesteamcoach, visualizer*, databasebackupmanager, aiprovider, "
             "main) route their user-visible strings through it.");
}

// The reflective test above passes even if translateFn() returns an empty QJSValue — the
// shape would be right and every binding in the app would still be undefined. This drives
// the REAL class and asserts the property value is callable and stable.
//
// `wrapper.property("translateString")` couples a string literal to a C++ method name with
// no compile-time link: an IDE rename of translateString() compiles clean, yields undefined,
// and turns every translated string into a TypeError. indexOfMethod catches removal, not a
// consistent rename. This catches both.
void TestTranslationReactivity::realTranslateFnIsCallableAndCached()
{
    QQmlEngine engine;
    QNetworkAccessManager nam;
    Settings settings;
    TranslationManager tm(&nam, &settings);

    tm.setJsEngine(&engine);

    const QJSValue fn = tm.translateFn();
    QVERIFY2(fn.isCallable(),
             "TranslationManager.translate is not a callable — QML call sites would raise a "
             "TypeError on every translated string. Did translateString() get renamed?");

    // Cached: rebuilding per read would allocate on every one of ~3,248 bindings each time
    // the language changes.
    QVERIFY2(fn.strictlyEquals(tm.translateFn()), "the callable must be built once and cached");

    // And it must actually translate — falling back to the English text when no translation
    // exists, which is the safety net the whole fallback argument rests on.
    const QJSValue out = fn.call({QJSValue("nonexistent.key.for.test"), QJSValue("Fallback Text")});
    QVERIFY2(!out.isError(), qPrintable(out.toString()));
    QCOMPARE(out.toString(), QStringLiteral("Fallback Text"));
}

QTEST_MAIN(TestTranslationReactivity)
#include "tst_translationreactivity.moc"
