#include <QJSEngine>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubator>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <memory>
#include <utility>
#include <private/qqmldata_p.h>
#include <private/qv4engine_p.h>
#include <private/qv4mm_p.h>
#include <private/qv4persistent_p.h>
#include <private/qv4qobjectwrapper_p.h>
#include "ProbeInteractionHost.h"
#include "ProbeNetwork.h"

namespace {
thread_local QStringList validationErrors;
QtMessageHandler previousHandler = nullptr;

void captureValidation(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith(QStringLiteral("Cross Validation Error")))
        validationErrors.append(message);
    if (previousHandler)
        previousHandler(type, context, message);
}

// Observe the real validator and sweep; do not replace their marking logic.
struct CollectionObserver {
    using Execute = QV4::GCState (*)(QV4::GCStateMachine *, QV4::GCStateInfo::ExtraData &);
    static thread_local CollectionObserver *current;
    QV4::GCStateMachine *gc;
    Execute validate;
    Execute sweep;
    int validations = 0;
    int sweeps = 0;

    explicit CollectionObserver(QV4::GCStateMachine *machine) : gc(machine)
    {
        Q_ASSERT(!current);
        current = this;
        validate = std::exchange(gc->stateInfoMap[QV4::GCState::CrossValidateIncrementalMarkPhase].execute,
                                observeValidation);
        sweep = std::exchange(gc->stateInfoMap[QV4::GCState::DoSweep].execute, observeSweep);
    }
    ~CollectionObserver()
    {
        gc->stateInfoMap[QV4::GCState::CrossValidateIncrementalMarkPhase].execute = validate;
        gc->stateInfoMap[QV4::GCState::DoSweep].execute = sweep;
        current = nullptr;
    }
    static QV4::GCState observeValidation(QV4::GCStateMachine *gc,
                                          QV4::GCStateInfo::ExtraData &data)
    {
        ++current->validations;
        return current->validate(gc, data);
    }
    static QV4::GCState observeSweep(QV4::GCStateMachine *gc, QV4::GCStateInfo::ExtraData &data)
    {
        auto next = current->sweep(gc, data);
        if (next == QV4::GCState::Invalid)
            ++current->sweeps;
        return next;
    }
};
thread_local CollectionObserver *CollectionObserver::current = nullptr;
}

class OwnershipRepro : public QObject {
    Q_OBJECT

    static bool advanceTo(QV4::GCStateMachine *gc, QV4::GCState target)
    {
        gc->deadline = QDeadlineTimer::Forever;
        for (int step = 0; step < 10000; ++step) {
            if (gc->state == target)
                return true;
            if (gc->state == QV4::GCState::Invalid)
                return false;
            auto &info = gc->stateInfoMap[int(gc->state)];
            gc->state = info.execute(gc, gc->stateData);
        }
        return false;
    }

private slots:
    void initTestCase()
    {
        previousHandler = qInstallMessageHandler(captureValidation);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(previousHandler);
    }

    void ownershipChange_data()
    {
        QTest::addColumn<bool>("cppOwnership");
        QTest::addColumn<bool>("afterWeakMark");
        QTest::newRow("adopt-before-weak-mark") << false << false;
        QTest::newRow("adopt-after-weak-mark") << false << true;
        QTest::newRow("promote-before-weak-mark") << true << false;
        QTest::newRow("promote-after-weak-mark") << true << true;
    }

    void ownershipChange()
    {
        QFETCH(bool, cppOwnership);
        QFETCH(bool, afterWeakMark);

        QObject cppRoot;
        QJSEngine engine;
        QObject root;
        QObject child(&root);
        QJSEngine::setObjectOwnership(&root, QJSEngine::JavaScriptOwnership);
        QJSEngine::setObjectOwnership(&child, QJSEngine::JavaScriptOwnership);
        auto *v4 = engine.handle();
        QV4::WeakValue wrapper(v4, QV4::Value::fromReturnedValue(
            QV4::QObjectWrapper::wrap(v4, &child)));

        const auto retainRoot = [&] {
            if (cppOwnership)
                QJSEngine::setObjectOwnership(&root, QJSEngine::CppOwnership);
            else
                root.setParent(&cppRoot);
        };
        if (!afterWeakMark)
            retainRoot();

        auto *mm = v4->memoryManager;
        mm->crossValidateIncrementalGC = true;
        auto *gc = mm->gcStateMachine.get();
        gc->reset();
        mm->gcBlocked = QV4::MemoryManager::NormalBlocked;
        // Pause at the same boundary as Qt's existing
        // tst_qv4mm::markObjectWrappersAfterMarkWeakValues (Qt 6.12.0:934).
        QVERIFY(advanceTo(gc, QV4::GCState::MarkDrain));
        if (afterWeakMark)
            retainRoot();
        QVERIFY(advanceTo(gc, QV4::GCState::MarkReady));
        const bool incrementallyMarked = wrapper.asManaged()->markBit();

        QVERIFY(mm->tryForceGCCompletion());
        const bool fullMarkRetained = !wrapper.isUndefined();
        // QObject parenting/ownership is the only mutation. No mark bits or
        // heap pointers are altered to manufacture a mismatch.
        QVERIFY2(fullMarkRetained, "Full marking did not retain the C++-owned subtree");
        QVERIFY2(incrementallyMarked,
                 "Incremental marking missed a wrapper retained by the full mark");
    }

    void sharedButtonCreation_data()
    {
        QTest::addColumn<int>("pauseStage");
        QTest::addColumn<bool>("stackView");
        for (bool stackView : {false, true}) {
            const QByteArray prefix = stackView ? "stack-" : "direct-";
            QTest::newRow((prefix + "before-weak-mark").constData())
                << int(QV4::GCState::InitMarkWeakValues) << stackView;
            QTest::newRow((prefix + "during-weak-mark").constData())
                << int(QV4::GCState::MarkWeakValues) << stackView;
            QTest::newRow((prefix + "after-weak-mark").constData())
                << int(QV4::GCState::MarkDrain) << stackView;
            QTest::newRow((prefix + "after-mark-drain").constData())
                << int(QV4::GCState::MarkReady) << stackView;
        }
    }

    void sharedButtonCreation()
    {
        QFETCH(int, pauseStage);
        QFETCH(bool, stackView);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray buttonSource = R"(
            SharedAccessibleButton {
                objectName: "probeButton"
                buttonStyle: SharedButtonStyle { bodyFont: Qt.application.font }
                interaction: SharedButtonInteraction {}
                accessibleName: "Compare with your last shot"
                text: "Compare"
                subtle: true
            }
        )";
        QByteArray source = "import QtQuick\nimport QtQuick.Controls\n";
        if (stackView) {
            source += R"(
                Item {
                    width: 400; height: 300
                    StackView { id: stack; anchors.fill: parent }
                    function pushPage() { stack.push(page) }
                    Component { id: page; Item {
            )" + buttonSource + "} } }";
        } else {
            source += buttonSource;
        }
        component.setData(source,
            QUrl::fromLocalFile(QStringLiteral(DECENZA_COMPONENTS_DIR "/OwnershipProbe.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> root;
        if (stackView) {
            root.reset(component.create());
            QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        }

        auto *v4 = engine.handle();
        auto *mm = v4->memoryManager;
        mm->crossValidateIncrementalGC = true;
        auto *gc = mm->gcStateMachine.get();
        gc->reset();
        mm->gcBlocked = QV4::MemoryManager::NormalBlocked;
        QVERIFY(advanceTo(gc, QV4::GCState(pauseStage)));
        if (stackView)
            QVERIFY(QMetaObject::invokeMethod(root.get(), "pushPage", Qt::DirectConnection));
        else
            root.reset(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        QObject *button = stackView ? root->findChild<QObject *>("probeButton") : root.get();
        QVERIFY(button);

        int inspected = 0;
        int missed = 0;
        for (QObject *child : button->findChildren<QObject *>()) {
            const QByteArray type(child->metaObject()->className());
            if (type != "QQuickSequentialAnimation" && type != "QQuickColorAnimation"
                    && type != "QQmlTimer")
                continue;
            QQmlData *data = QQmlData::get(child);
            QVERIFY(data && !data->jsWrapper.isUndefined());
            ++inspected;
            if (gc->inProgress() && !data->jsWrapper.asManaged()->markBit())
                ++missed;
        }
        qInfo("Shared button creation: inspected=%d missed=%d collectionStillActive=%d",
              inspected, missed, gc->inProgress());
        QVERIFY(inspected >= 5);
        if (gc->inProgress())
            QVERIFY(mm->tryForceGCCompletion());
        QCOMPARE(missed, 0);
    }

    void compiledReviewTransitions_data()
    {
        QTest::addColumn<int>("pauseStage");
        QTest::addColumn<int>("operation");
        for (auto stage : {QV4::GCState::MarkJSStack, QV4::GCState::MarkPersistentValues,
                           QV4::GCState::MarkWeakValues, QV4::GCState::MarkDrain,
                           QV4::GCState::MarkReady}) {
            for (int operation = 0; operation < 3; ++operation) {
                const QByteArray name = QByteArray::number(int(stage)) + "-op-"
                    + QByteArray::number(operation);
                QTest::newRow(name.constData()) << int(stage) << operation;
            }
        }
    }

    void compiledReviewTransitions()
    {
        QFETCH(int, pauseStage);
        QFETCH(int, operation);
        QQmlEngine engine;
        QQmlComponent component(&engine, "OwnershipProbe", "ReplayPage");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(800, 600);
        window.show();
        window.requestActivate();
        QCoreApplication::processEvents();
        auto *v4 = engine.handle();
        auto *mm = v4->memoryManager;
        mm->crossValidateIncrementalGC = true;
        auto *gc = mm->gcStateMachine.get();
        std::unique_ptr<QObject> page;
        int inspected = 0;
        for (int cycle = 0; cycle < 30; ++cycle) {
            if (operation != 0) {
                page.reset(component.create());
                QVERIFY2(page != nullptr, qPrintable(component.errorString()));
                qobject_cast<QQuickItem *>(page.get())->setParentItem(window.contentItem());
                if (operation == 2) {
                    QVERIFY(QMetaObject::invokeMethod(page.get(), "finishLoad"));
                    QVERIFY(QMetaObject::invokeMethod(page.get(), "focusCompare"));
                }
            }

            validationErrors.clear();
            gc->reset();
            mm->gcBlocked = QV4::MemoryManager::NormalBlocked;
            QVERIFY(advanceTo(gc, QV4::GCState(pauseStage)));
            if (operation == 0) {
                page.reset(component.create());
                QVERIFY2(page != nullptr, qPrintable(component.errorString()));
                qobject_cast<QQuickItem *>(page.get())->setParentItem(window.contentItem());
            }
            if (operation == 2)
                QVERIFY(QMetaObject::invokeMethod(page.get(), "clearLoad"));
            QVERIFY(QMetaObject::invokeMethod(page.get(), "finishLoad"));
            QVERIFY(QMetaObject::invokeMethod(page.get(), "focusCompare"));
            QVERIFY(advanceTo(gc, QV4::GCState::MarkReady));

            QObject *button = page->findChild<QObject *>("reviewCompare");
            QVERIFY(button);
            QVERIFY(qobject_cast<QQuickItem *>(button)->hasActiveFocus());
            int trackedThisCycle = 0;
            for (QObject *child : button->findChildren<QObject *>()) {
                const QByteArray type(child->metaObject()->className());
                if (type != "QQuickSequentialAnimation" && type != "QQuickColorAnimation"
                        && type != "QQmlTimer")
                    continue;
                QQmlData *data = QQmlData::get(child);
                QVERIFY(data && !data->jsWrapper.isUndefined());
                QVERIFY2(data->jsWrapper.asManaged()->markBit(), child->metaObject()->className());
                if (type == "QQuickSequentialAnimation")
                    QVERIFY(child->property("running").toBool());
                ++trackedThisCycle;
            }
            QVERIFY(trackedThisCycle >= 5);
            inspected += trackedThisCycle;
            QVERIFY(mm->tryForceGCCompletion());
            QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
            page.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
        qInfo("Compiled review: stage=%d operation=%d cycles=30 inspected=%d",
              pauseStage, operation, inspected);
    }

    void asynchronousCreation_data()
    {
        QTest::addColumn<bool>("cancelCreation");
        QTest::newRow("complete") << false;
        QTest::newRow("cancel") << true;
    }

    void asynchronousCreation()
    {
        QFETCH(bool, cancelCreation);
        QQmlEngine engine;
        QQmlIncubationController controller;
        engine.setIncubationController(&controller);
        QQmlComponent component(&engine);
        QByteArray source = "import QtQuick\nimport OwnershipProbe\nItem {\n";
        for (int i = 0; i < 100; ++i)
            source += "ReplayPage {}\n";
        source += "}";
        component.setData(source, QUrl("file:///asynchronous-review-probe.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto *mm = engine.handle()->memoryManager;
        mm->crossValidateIncrementalGC = true;
        int collectionsDuringCreation = 0;
        int canceled = 0;
        validationErrors.clear();
        for (int cycle = 0; cycle < 10; ++cycle) {
            QQmlIncubator incubator(QQmlIncubator::Asynchronous);
            component.create(incubator);
            int slices = 0;
            while (incubator.isLoading() && slices < 10000) {
                controller.incubateFor(0);
                ++slices;
                if (incubator.isLoading()) {
                    ++collectionsDuringCreation;
                    mm->runFullGC();
                    if (cancelCreation && slices == 3) {
                        incubator.clear();
                        ++canceled;
                    }
                }
                QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
            }
            QVERIFY(slices < 10000);
            QVERIFY2(!incubator.isError(), qPrintable(incubator.errors().isEmpty()
                ? QString() : incubator.errors().first().toString()));
            if (!cancelCreation) {
                QVERIFY(incubator.isReady());
                std::unique_ptr<QObject> root(incubator.object());
                QCOMPARE(root->findChildren<QObject *>("reviewCompare").size(), 100);
            }
            mm->runFullGC();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
        }
        qInfo("Asynchronous creation: cancel=%d collectionsWhileLoading=%d canceled=%d",
              cancelCreation, collectionsDuringCreation, canceled);
        QVERIFY(collectionsDuringCreation > 0);
        if (cancelCreation)
            QCOMPARE(canceled, 10);
    }

    void partialWeakScan_data()
    {
        QTest::addColumn<int>("batches");
        QTest::addColumn<bool>("retireObjects");
        QTest::addColumn<bool>("bindingErrors");
        for (int batches : {1, 3, 5}) {
            for (bool retire : {false, true}) {
                const auto name = QByteArray::number(batches) + (retire ? "-retire" : "-create");
                QTest::newRow(name.constData()) << batches << retire << false;
                QTest::newRow((name + "-errors").constData()) << batches << retire << true;
            }
        }
    }

    void partialWeakScan()
    {
        QFETCH(int, batches);
        QFETCH(bool, retireObjects);
        QFETCH(bool, bindingErrors);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        if (bindingErrors) {
            component.setData(R"(
                import QtQuick
                import OwnershipProbe
                ReplayPage {
                    property string badMount: pa.mountingMode
                    property bool badVisible: pa.meanTempC !== undefined
                    property string badText: pa.meanTempC.toFixed(1)
                }
            )", QUrl("file:///faulty-review-probe.qml"));
        } else {
            component.loadFromModule("OwnershipProbe", "ReplayPage");
        }
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QObject cppRoot;
        std::vector<std::unique_ptr<QObject>> population;
        for (int i = 0; i < 8192; ++i) {
            auto object = std::make_unique<QObject>(&cppRoot);
            QJSEngine::setObjectOwnership(object.get(), QJSEngine::JavaScriptOwnership);
            engine.newQObject(object.get());
            population.push_back(std::move(object));
        }
        auto *mm = engine.handle()->memoryManager;
        mm->runFullGC();
        mm->crossValidateIncrementalGC = true;
        auto *gc = mm->gcStateMachine.get();
        validationErrors.clear();
        gc->reset();
        mm->gcBlocked = QV4::MemoryManager::NormalBlocked;
        QVERIFY(advanceTo(gc, QV4::GCState::MarkWeakValues));
        for (int batch = 0; batch < batches; ++batch) {
            gc->state = gc->stateInfoMap[QV4::GCState::MarkWeakValues].execute(gc, gc->stateData);
            QCOMPARE(gc->state, QV4::GCState::MarkWeakValues);
        }
        int markedBefore = 0;
        for (const auto &object : population) {
            auto *data = QQmlData::get(object.get());
            QVERIFY(data && !data->jsWrapper.isUndefined());
            markedBefore += data->jsWrapper.asManaged()->markBit();
        }
        QVERIFY(markedBefore > 0 && markedBefore < 8192);
        if (retireObjects) {
            for (size_t i = 0; i < population.size(); i += 2)
                population[i].reset();
        }
        std::vector<std::unique_ptr<QObject>> pages;
        for (int i = 0; i < 100; ++i) {
            if (bindingErrors) {
                for (int warning = 0; warning < 3; ++warning)
                    QTest::ignoreMessage(QtWarningMsg,
                        QRegularExpression(".*ReferenceError: pa is not defined.*"));
            }
            std::unique_ptr<QObject> page(component.create());
            QVERIFY2(page != nullptr, qPrintable(component.errorString()));
            QVERIFY(QMetaObject::invokeMethod(page.get(), "finishLoad"));
            pages.push_back(std::move(page));
        }
        QVERIFY(advanceTo(gc, QV4::GCState::MarkReady));
        int inspected = 0;
        for (const auto &page : pages) {
            auto *button = page->findChild<QObject *>("reviewCompare");
            QVERIFY(button);
            for (QObject *child : button->findChildren<QObject *>()) {
                const QByteArray type(child->metaObject()->className());
                if (type != "QQuickSequentialAnimation" && type != "QQuickColorAnimation"
                        && type != "QQmlTimer")
                    continue;
                auto *data = QQmlData::get(child);
                QVERIFY(data && !data->jsWrapper.isUndefined());
                QVERIFY2(data->jsWrapper.asManaged()->markBit(), child->metaObject()->className());
                ++inspected;
            }
        }
        QCOMPARE(inspected, 500);
        QVERIFY(mm->tryForceGCCompletion());
        QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
        qInfo("Partial weak scan: batches=%d markedBefore=%d retired=%d bindingErrors=%d pages=100 wrappers=%d",
              batches, markedBefore, retireObjects ? 4096 : 0, bindingErrors ? 300 : 0, inspected);
    }

    void xhrCallbackLifetime_data();
    void xhrCallbackLifetime();

    void qmlDestructionAllocations()
    {
        QQmlEngine engine;
        auto *mm = engine.handle()->memoryManager;
        mm->crossValidateIncrementalGC = true;
        auto *host = engine.singletonInstance<ProbeInteractionHost *>(
            "OwnershipProbe", "ProbeInteractionHost");
        QVERIFY(host);
        QMap<int, int> stages;
        QObject observer;
        connect(host, &ProbeInteractionHost::destructionRecorded, &observer, [&] {
            ++stages[int(mm->gcStateMachine->state)];
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import OwnershipProbe
            QtObject {
                Component.onDestruction: {
                    ProbeInteractionHost.destructionRecorded()
                    let values = []
                    for (let i = 0; i < 256; ++i)
                        values.push({index: i, text: "destruction allocation " + i})
                    ProbeInteractionHost.retained = values
                }
            }
        )", QUrl("file:///qml-destruction-probe.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        validationErrors.clear();
        for (int cycle = 0; cycle < 100; ++cycle) {
            for (int item = 0; item < 16; ++item) {
                QObject *object = component.create();
                QVERIFY2(object, qPrintable(component.errorString()));
                QJSEngine::setObjectOwnership(object, QJSEngine::JavaScriptOwnership);
                // The temporary wrapper goes out of scope; normal Qt GC owns deletion.
                engine.newQObject(object);
            }
            mm->runFullGC();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
        }
        int total = 0;
        for (auto it = stages.cbegin(); it != stages.cend(); ++it) {
            qInfo("QML destruction: GC stage=%d callbacks=%d", it.key(), it.value());
            total += it.value();
        }
        QCOMPARE(total, 1600);
        QCOMPARE(host->retained.property("length").toInt(), 256);
    }

    void automaticReviewStress_data()
    {
        QTest::addColumn<int>("budgetUs");
        QTest::addColumn<bool>("stackView");
        QTest::newRow("direct-one-microsecond") << 1 << false;
        QTest::newRow("direct-one-millisecond") << 1000 << false;
        QTest::newRow("stack-one-microsecond") << 1 << true;
        QTest::newRow("stack-one-millisecond") << 1000 << true;
    }

    void automaticReviewStress()
    {
        QFETCH(int, budgetUs);
        QFETCH(bool, stackView);
        QQmlEngine engine;
        QQmlComponent component(&engine, "OwnershipProbe", stackView ? "ReplayStack" : "ReplayPage");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(800, 600);
        window.show();
        window.requestActivate();
        QCoreApplication::processEvents();
        auto *mm = engine.handle()->memoryManager;
        auto *gc = mm->gcStateMachine.get();
        mm->crossValidateIncrementalGC = true;
        gc->timeLimit = std::chrono::microseconds(budgetUs);
        CollectionObserver observer(gc);
        QJSValue allocate = engine.evaluate(QStringLiteral(R"(
            (function () {
                let ring = [], index = 0;
                return function () {
                    let values = [];
                    for (let i = 0; i < 2048; ++i)
                        values.push({ index: i, values: [i, i + 1, i + 2] });
                    ring[index++ % 32] = values;
                };
            })()
        )"));
        QVERIFY(allocate.isCallable());
        validationErrors.clear();
        std::unique_ptr<QObject> page;
        auto *host = engine.singletonInstance<ProbeInteractionHost *>(
            "OwnershipProbe", "ProbeInteractionHost");
        QVERIFY(host);
        int updatesDuringMark = 0;
        int updatesAfterWeakMark = 0;
        int focusedAnimations = 0;
        for (int cycle = 0; cycle < 2000; ++cycle) {
            QVERIFY2(!allocate.call().isError(), "Allocation workload threw a JavaScript exception");
            if (cycle % 8 == 0) {
                if (stackView && page) {
                    QVERIFY(QMetaObject::invokeMethod(page.get(), "reopen"));
                } else {
                    page.reset(component.create());
                    QVERIFY2(page != nullptr, qPrintable(component.errorString()));
                    qobject_cast<QQuickItem *>(page.get())->setParentItem(window.contentItem());
                }
            }
            if (gc->inProgress() && gc->state <= QV4::GCState::MarkReady) {
                ++updatesDuringMark;
                if (gc->state > QV4::GCState::MarkWeakValues)
                    ++updatesAfterWeakMark;
            }
            QVERIFY(QMetaObject::invokeMethod(page.get(), "clearLoad"));
            QVERIFY(QMetaObject::invokeMethod(page.get(), "finishLoad"));
            QVERIFY(QMetaObject::invokeMethod(page.get(), "focusCompare"));
            auto *button = qobject_cast<QQuickItem *>(page->property("compareButton").value<QObject *>());
            QVERIFY(button && button->hasActiveFocus());
            QCOMPARE(host->lastAnnouncedItem(), button);
            for (QObject *child : button->findChildren<QObject *>()) {
                if (QByteArray(child->metaObject()->className()) == "QQuickSequentialAnimation") {
                    QVERIFY(child->property("running").toBool());
                    ++focusedAnimations;
                }
            }
            // Let queued incremental steps, layout and deferred destruction run.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
        }
        qInfo("Automatic review: budgetUs=%d stackView=%d updates=2000 pages=250 validations=%d sweeps=%d "
              "updatesDuringMark=%d updatesAfterWeakMark=%d focusedAnimations=%d",
              budgetUs, stackView, observer.validations, observer.sweeps,
              updatesDuringMark, updatesAfterWeakMark, focusedAnimations);
        QVERIFY(observer.validations > 0);
        QVERIFY(observer.sweeps > 0);
        // At the larger budget this small workload can finish marking inside
        // allocation, before the next UI update. Keep that as a control.
        if (budgetUs == 1) {
            QVERIFY(updatesDuringMark > 0);
            QVERIFY(updatesAfterWeakMark > 0);
        }
        QCOMPARE(focusedAnimations, 2000);
        if (gc->inProgress())
            QVERIFY(mm->tryForceGCCompletion());
        QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
    }
};

void OwnershipRepro::xhrCallbackLifetime_data()
{
    QTest::addColumn<bool>("destroyPage");
    QTest::addColumn<bool>("invalidJson");
    QTest::newRow("complete-json") << false << false;
    QTest::newRow("complete-json-error") << false << true;
    QTest::newRow("deleted-page-json") << true << false;
    QTest::newRow("deleted-page-json-error") << true << true;
}

void OwnershipRepro::xhrCallbackLifetime()
{
    QFETCH(bool, destroyPage);
    QFETCH(bool, invalidJson);
    ProbeNetworkFactory factory;
    QQmlEngine engine;
    engine.setNetworkAccessManagerFactory(&factory);
    auto *host = engine.singletonInstance<ProbeInteractionHost *>(
        "OwnershipProbe", "ProbeInteractionHost");
    QVERIFY(host);
    QQmlComponent component(&engine);
    // v2.0.8 ShotMapScreensaver.qml:59-80 uses this xhr-capturing closure.
    // Preserve its lifetime and JSON handling, without the map or services.
    component.setData(R"(
        import QtQml
        import OwnershipProbe
        QtObject {
            property var shots: []
            function fetchShots() {
                var xhr = new XMLHttpRequest()
                xhr.onreadystatechange = function() {
                    if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                        try {
                            let data = JSON.parse(xhr.responseText)
                            shots = data.shots || []
                            ProbeInteractionHost.retained = shots
                        } catch (e) {
                            ProbeInteractionHost.retained = String(e)
                        }
                    }
                }
                xhr.open("GET", "https://probe.invalid/shots.json")
                xhr.send()
            }
        }
    )", QUrl("file:///xhr-lifetime-probe.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QByteArray body = "{\"shots\":[";
    for (int i = 0; i < 512; ++i) {
        if (i)
            body += ',';
        body += "{\"latitude\":42,\"longitude\":-105,\"profile\":\"quality-test\"}";
    }
    body += "]}";
    if (invalidJson)
        body = "invalid JSON";
    auto *mm = engine.handle()->memoryManager;
    mm->crossValidateIncrementalGC = true;
    auto *gc = mm->gcStateMachine.get();
    CollectionObserver observer(gc);
    const QV4::GCState stages[] = {QV4::GCState::MarkGlobalObject,
        QV4::GCState::MarkPersistentValues, QV4::GCState::MarkWeakValues,
        QV4::GCState::MarkDrain, QV4::GCState::MarkReady};
    validationErrors.clear();
    for (int cycle = 0; cycle < 100; ++cycle) {
        host->retained = QJSValue();
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(page.get(), "fetchShots"));
        QVERIFY(factory.reply);
        // Collect while the reply is pending, then finish it during marking.
        mm->runFullGC();
        gc->reset();
        mm->gcBlocked = QV4::MemoryManager::NormalBlocked;
        QVERIFY(advanceTo(gc, stages[cycle % 5]));
        if (destroyPage)
            page.reset();
        factory.reply->deliver(body);
        if (destroyPage) {
            if (cycle == 0)
                qInfo() << "Reply after page deletion:" << host->retained.toString();
            QVERIFY(host->retained.isString());
        } else if (invalidJson)
            QVERIFY(host->retained.toString().contains("SyntaxError"));
        else
            QCOMPARE(host->retained.property("length").toInt(), 512);
        QVERIFY(mm->tryForceGCCompletion());
        page.reset();
        host->retained = QJSValue();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        mm->runFullGC();
        QVERIFY2(validationErrors.isEmpty(), qPrintable(validationErrors.join('\n')));
    }
    QVERIFY(observer.validations >= 100);
    qInfo("XHR lifetime: deleted=%d invalidJson=%d requests=100 validations=%d sweeps=%d",
          destroyPage, invalidJson, observer.validations, observer.sweeps);
}


QTEST_MAIN(OwnershipRepro)
#include "tst_qmlgc_ownership.moc"
