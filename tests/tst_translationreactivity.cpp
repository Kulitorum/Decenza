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
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>

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
    // callable from C++ (11 call sites) regardless of the QML-facing shape.
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
    void asyncResolutionPatternReRenders();
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

// design.md D4: the emoji resolver is asynchronous, so its result changes when a
// fetch lands. That is the same dependency problem wearing a different hat — this
// asserts the one mechanism covers both, so fixing translations does not ship a
// fresh class of stale binding beside it.
void TestTranslationReactivity::asyncResolutionPatternReRenders()
{
    QQmlEngine engine;
    SpikeTranslator resolver(&engine);
    // "" models an unresolved asset (strip); the later value models the fetch landing.
    resolver.setTranslation("emoji:1f9cb", "");
    engine.rootContext()->setContextProperty("R", &resolver);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        QtObject { property string src: R.translate("emoji:1f9cb", "") }
    )", QUrl("qrc:/spike_async.qml"));

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, qPrintable(component.errorString()));
    QCOMPARE(obj->property("src").toString(), QString());

    // Fetch completes.
    resolver.setTranslation("emoji:1f9cb", "file:///cache/1f9cb.svg");
    QCOMPARE(obj->property("src").toString(), QStringLiteral("file:///cache/1f9cb.svg"));
}

QTEST_MAIN(TestTranslationReactivity)
#include "tst_translationreactivity.moc"
