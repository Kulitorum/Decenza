// Copyright (C) 2016 basysKom GmbH.
// SPDX-License-Identifier: GPL-3.0-only
// Regression scenario adapted from Qt's QTBUG-149512 test:
// https://github.com/qt/qtdeclarative/commit/059ee9db91507b8b86cdf4bf033985c9ea41e0d4
#include <QJSEngine>
#include <QtTest>
#include <private/qv4engine_p.h>
#include <private/qv4mm_p.h>
#include <private/qv4object_p.h>
#include <private/qv4persistent_p.h>
#include <vector>

struct SweepAllocations {
    std::vector<QV4::PersistentValue> roots;
    int allocationsInSweptWord = 0;
};

namespace QV4 {
namespace Heap {
struct ProbeSweepAllocation : Object {
    SweepAllocations *result;
    void init(SweepAllocations *output) { Object::init(); result = output; }
    void destroy()
    {
        auto *engine = internalClass->engine;
        const auto *self = reinterpret_cast<const HeapItem *>(this);
        auto *chunk = self->chunk();
        const auto word = size_t(self - chunk->realBase()) / Chunk::Bits;
        for (int i = 0; i < 256; ++i) {
            auto *object = engine->newObject();
            const auto *item = reinterpret_cast<const HeapItem *>(object);
            if (item->chunk() == chunk
                    && size_t(item - chunk->realBase()) / Chunk::Bits == word) {
                ++result->allocationsInSweptWord;
                result->roots.emplace_back(engine, Value::fromHeapObject(object));
            }
        }
        Object::destroy();
    }
};
}
struct ProbeSweepAllocation : Object {
    V4_OBJECT2(ProbeSweepAllocation, Object)
    V4_NEEDS_DESTROY
};
DEFINE_OBJECT_VTABLE(ProbeSweepAllocation);
}

class SweepRepro : public QObject {
    Q_OBJECT
private slots:
    void liveAllocationsKeepTheirSlots_data()
    {
        QTest::addColumn<bool>("duringSweep");
        QTest::newRow("allocate-before-sweep") << false;
        QTest::newRow("allocate-during-sweep") << true;
    }

    void liveAllocationsKeepTheirSlots()
    {
        QFETCH(bool, duringSweep);
        QJSEngine engine;
        auto *v4 = engine.handle();
        auto *mm = v4->memoryManager;
        mm->gcStateMachine->timeLimit = std::chrono::microseconds(0);
        std::vector<QV4::PersistentValue> survivors;
        mm->gcBlocked = QV4::MemoryManager::InCriticalSection;
        for (int i = 0; i < 2048; ++i) {
            auto *object = v4->newObject();
            if (i % 2)
                survivors.emplace_back(v4, QV4::Value::fromHeapObject(object));
        }
        mm->gcBlocked = QV4::MemoryManager::Unblocked;
        mm->runFullGC();

        SweepAllocations result;
        if (duringSweep) {
            mm->allocate<QV4::ProbeSweepAllocation>(&result);
        } else {
            for (int i = 0; i < 256; ++i)
                result.roots.emplace_back(v4, QV4::Value::fromHeapObject(v4->newObject()));
        }
        mm->runFullGC();
        if (duringSweep)
            QVERIFY(result.allocationsInSweptWord > 0);
        QVERIFY(!result.roots.empty());

        int lostExtentSlots = 0;
        int freeRunsInsideLiveObjects = 0;
        for (const auto &root : result.roots) {
            auto *managed = root.asManaged();
            const auto *item = reinterpret_cast<const QV4::HeapItem *>(managed->heapObject());
            auto *chunk = item->chunk();
            const size_t index = item - chunk->realBase();
            QVERIFY(QV4::Chunk::testBit(chunk->objectBitmap, index));
            const auto *table = managed->vtable();
            const size_t bytes = (table->nInlineProperties + table->inlinePropertyOffset)
                * sizeof(QV4::Value);
            const size_t slotCount = (bytes + QV4::Chunk::SlotSize - 1) / QV4::Chunk::SlotSize;
            for (size_t tail = 1; tail < slotCount; ++tail) {
                if (!QV4::Chunk::testBit(chunk->extendsBitmap, index + tail))
                    ++lostExtentSlots;
            }
            const auto begin = quintptr(item);
            const auto end = begin + slotCount * sizeof(QV4::HeapItem);
            for (int bin = 0; bin < QV4::BlockAllocator::NumBins; ++bin) {
                for (auto *free = mm->blockAllocator.freeBins[bin]; free; free = free->freeData.next) {
                    if (quintptr(free) > begin && quintptr(free) < end)
                        ++freeRunsInsideLiveObjects;
                }
            }
        }
        qInfo("Sweep allocation: duringSweep=%d roots=%zu inSweptWord=%d lostExtentSlots=%d "
              "freeRunsInsideLiveObjects=%d", duringSweep, result.roots.size(),
              result.allocationsInSweptWord, lostExtentSlots, freeRunsInsideLiveObjects);
        QCOMPARE(lostExtentSlots, 0);
        QCOMPARE(freeRunsInsideLiveObjects, 0);
    }
};

QTEST_GUILESS_MAIN(SweepRepro)
#include "tst_qmlgc_sweep.moc"
