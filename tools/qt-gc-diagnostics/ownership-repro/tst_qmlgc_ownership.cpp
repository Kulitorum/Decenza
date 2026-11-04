#include <QJSEngine>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <memory>
#include <private/qqmldata_p.h>
#include <private/qv4engine_p.h>
#include <private/qv4mm_p.h>
#include <private/qv4persistent_p.h>
#include <private/qv4qobjectwrapper_p.h>

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
};

QTEST_MAIN(OwnershipRepro)
#include "tst_qmlgc_ownership.moc"
