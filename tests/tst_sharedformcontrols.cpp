#include <QtQuickTest/quicktest.h>
#include <QAccessible>
#include <QFile>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopeGuard>
#include <algorithm>
#include <memory>

#include "accessiblevaluechange.h"

class NumericAccessibilityRegression : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void hostEchoesAndCommittedSteps();
};

void NumericAccessibilityRegression::hostEchoesAndCommittedSteps()
{
    QFile file(QFINDTESTDATA("../qml/components/ValueInput.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(file.readAll());
    const auto liftFunction = [](const QString& text, const QString& signature) {
        const qsizetype start = text.indexOf(signature);
        if (start < 0) return QString();
        int depth = 0;
        for (qsizetype i = text.indexOf('{', start); i >= 0 && i < text.size(); ++i) {
            if (text[i] == '{') ++depth;
            else if (text[i] == '}' && --depth == 0) return text.mid(start, i - start + 1);
        }
        return QString();
    };
    const qsizetype headerStart = source.indexOf(QStringLiteral("    // Value properties"));
    const qsizetype headerEnd = source.indexOf(QStringLiteral("    // Enable keyboard focus"), headerStart);
    const qsizetype semanticsStart = source.indexOf(QStringLiteral("    // Accessibility - expose"));
    const qsizetype semanticsEnd = source.indexOf(QStringLiteral("    // Keyboard handling."), semanticsStart);
    const qsizetype popupStart = source.lastIndexOf(QStringLiteral("                            Item {"),
        source.indexOf(QStringLiteral("id: popupValueContainer")));
    const qsizetype popupEnd = source.indexOf(QStringLiteral("                                // Keyboard navigation"), popupStart);
    QVERIFY(headerStart >= 0 && headerEnd > headerStart);
    QVERIFY(semanticsStart >= 0 && semanticsEnd > semanticsStart);
    QVERIFY(popupStart >= 0 && popupEnd > popupStart);

    // Run the shipping state/handlers and both slider interfaces. Only the
    // unrelated visual tree, translation and Theme tokens are fixture inputs.
    QString header = source.mid(headerStart, headerEnd - headerStart);
    header.replace(QStringLiteral("property real value: 0"), QStringLiteral("property real value: hostValue"));
    QString semantics = source.mid(semanticsStart, semanticsEnd - semanticsStart);
    QString popup = source.mid(popupStart, popupEnd - popupStart) + QStringLiteral("\n}");
    QString functions;
    for (const QString& name : {QStringLiteral("gearToStep"), QStringLiteral("adjustValue"),
                                QStringLiteral("adjustValueWithStep")})
        functions += liftFunction(source, QStringLiteral("function %1(").arg(name)) + QLatin1Char('\n');
    for (QString* block : {&header, &semantics, &popup, &functions}) {
        block->replace(QStringLiteral("AccessibleValueChange"), QStringLiteral("root.notifier"));
        block->replace(QStringLiteral("TranslationManager"), QStringLiteral("root.translator"));
        block->replace(QStringLiteral("Theme."), QStringLiteral("root.themeMock."));
        block->replace(QStringLiteral("AccessibilityManager"), QStringLiteral("root.accessibilityMock"));
    }

    struct NumericEvent {
        QPointer<QObject> object;
        QVariant value;
        QAccessible::Id id;
        int child;
        QPointer<QObject> resolvedObject;
    };
    static QList<NumericEvent> events;
    events.clear();
    const bool wasActive = QAccessible::isActive();
    QAccessible::setActive(true);
    const auto oldHandler = QAccessible::installUpdateHandler([](QAccessibleEvent* event) {
        if (event->type() == QAccessible::ValueChanged) {
            // Cocoa resolves uniqueId(), not just accessibleInterface()
            // (qtbase/src/plugins/platforms/cocoa/qcocoaaccessibility.mm:29–35).
            // The interface event constructor could pass the old recorder but
            // fail this resolution by interpreting its ID as a child index.
            const auto id = event->uniqueId();
            auto* resolved = QAccessible::accessibleInterface(id);
            events.append({event->object(), static_cast<QAccessibleValueChangeEvent*>(event)->value(),
                           id, event->child(), resolved ? resolved->object() : nullptr});
        }
    });
    const auto restoreAccessibility = qScopeGuard([oldHandler, wasActive] {
        QAccessible::installUpdateHandler(oldHandler);
        QAccessible::setActive(wasActive);
        events.clear();
    });

    QQmlEngine engine;
    auto* notifier = AccessibleValueChange::create(&engine, &engine);
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"QML(
        import QtQuick
        import QtQuick.Layouts
        Item {
            id: root
            property QtObject notifier
            property real hostValue: 200
            property bool echo: false
            property int modifications: 0
            property int commits: 0
            property real requested: -1
            property QtObject themeMock: QtObject {
                property color textColor: "black"
                property color primaryColor: "blue"
                function scaled(value) { return value }
                function scaledBase(value) { return value }
            }
            property QtObject translator: QtObject {
                function translate(key, fallback) { return fallback }
            }
            property QtObject accessibilityMock: QtObject { property bool enabled: false }
            %1
            %2
            onValueModified: function(next) {
                ++modifications
                requested = next
                if (echo) hostValue = next
            }
            onValueCommitted: function(next) { ++commits; requested = next }
            Item {
                id: popupContent
                property int currentGear: 0
                property bool editMode: false
                function adjust(steps) { root.adjustValueWithStep(steps, root.gearToStep(currentGear)) }
                %3
            }
            %4
        }
    )QML").arg(header, semantics, popup, functions).toUtf8(), QUrl());
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {QStringLiteral("notifier"), QVariant::fromValue(notifier)},
        {QStringLiteral("to"), 1000.0}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* popupItem = object->findChild<QQuickItem*>();
    QVERIFY(popupItem);
    // The first child is popupContent; its child is the shipping slider body.
    popupItem = popupItem->findChild<QQuickItem*>();
    QVERIFY(popupItem);
    auto* iface = QAccessible::queryAccessibleInterface(object.get());
    auto* popupIface = QAccessible::queryAccessibleInterface(popupItem);
    QVERIFY(iface && popupIface && iface->valueInterface() && popupIface->valueInterface());
    QCOMPARE(iface->role(), QAccessible::Slider);
    QCOMPARE(popupIface->role(), QAccessible::Slider);
    const auto sawValue = [&](QObject* target, double value) {
        return std::any_of(events.cbegin(), events.cend(), [=](const NumericEvent& event) {
            return event.object == target && event.value.toDouble() == value
                && event.id != 0 && event.child == -1 && event.resolvedObject == target;
        });
    };
    QVERIFY(sawValue(object.get(), 200)); // Completion covers values bound before role assignment.
    QVERIFY(sawValue(popupItem, 200));
    QCOMPARE(popupIface->valueInterface()->minimumValue().toDouble(), 0.0);
    QCOMPARE(popupIface->valueInterface()->maximumValue().toDouble(), 1000.0);
    for (const double value : {500.0, 200.0, 40.0, 80.0}) {
        events.clear();
        QVERIFY(object->setProperty("hostValue", value));
        QVERIFY(sawValue(object.get(), value));
        QVERIFY(sawValue(popupItem, value));
        QCOMPARE(iface->valueInterface()->currentValue().toDouble(), value);
        QCOMPARE(popupIface->valueInterface()->currentValue().toDouble(), value);
    }
    QCOMPARE(object->property("modifications").toInt(), 0);
    QCOMPARE(object->property("commits").toInt(), 0);

    events.clear();
    iface->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
    QCOMPARE(object->property("requested").toDouble(), 81.0);
    QCOMPARE(object->property("modifications").toInt(), 1);
    QCOMPARE(object->property("commits").toInt(), 1);
    QCOMPARE(iface->valueInterface()->currentValue().toDouble(), 80.0); // Await host echo.
    QVERIFY(!sawValue(object.get(), 81));
    QVERIFY(object->setProperty("hostValue", 81.0));
    QVERIFY(sawValue(object.get(), 81)); // The consumer binding survived the action.
    QVERIFY(object->setProperty("echo", true));
    iface->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());
    QCOMPARE(object->property("hostValue").toDouble(), 80.0);
    QCOMPARE(object->property("commits").toInt(), 2);

    QVERIFY(popupItem->parent()->setProperty("currentGear", 1));
    QCOMPARE(popupIface->valueInterface()->minimumStepSize().toDouble(), 10.0);
    popupIface->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
    QCOMPARE(object->property("hostValue").toDouble(), 90.0);
    QCOMPARE(object->property("commits").toInt(), 3);
    popupIface->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());
    QCOMPARE(object->property("hostValue").toDouble(), 80.0);
    QCOMPARE(object->property("commits").toInt(), 4);
    QVERIFY(object->setProperty("hostValue", 1000.0));
    iface->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
    QCOMPARE(object->property("commits").toInt(), 4); // A pinned step is a no-op.

    QAccessible::setActive(false);
    events.clear();
    QVERIFY(object->setProperty("hostValue", 65.0));
    QVERIFY(events.isEmpty());
    QAccessible::setActive(true);
    QCOMPARE(iface->valueInterface()->currentValue().toDouble(), 65.0);
    notifier->notifyNumericValueChanged(object.get());
    QVERIFY(sawValue(object.get(), 65));
}

class SharedFormControlsSetup : public QObject
{
    Q_OBJECT
public:
    int numericResult = 0;

public slots:
    void applicationAvailable()
    {
        NumericAccessibilityRegression regression;
        numericResult = QTest::qExec(&regression, QStringList{QStringLiteral("tst_sharedformcontrols_numeric")});
    }
};

int main(int argc, char** argv)
{
    QTEST_SET_MAIN_SOURCE_PATH
    SharedFormControlsSetup setup;
#ifdef QUICK_TEST_SOURCE_DIR
    constexpr const char* sourceDir = QUICK_TEST_SOURCE_DIR;
#else
    constexpr const char* sourceDir = nullptr;
#endif
    // Qt creates QGuiApplication before applicationAvailable() and starts QML
    // tests afterward (qtdeclarative/src/qmltest/quicktest.cpp:437–448,613–615).
    const int quickResult = quick_test_main_with_setup(argc, argv, "tst_sharedformcontrols", sourceDir, &setup);
    return quickResult ? quickResult : setup.numericResult;
}

#include "tst_sharedformcontrols.moc"
