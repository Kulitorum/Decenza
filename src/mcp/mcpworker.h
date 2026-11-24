#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QThread>

#include <functional>
#include <memory>

// Runs `work` on a worker thread, then calls `respond` with its result on the
// main thread.
//
// The response and the thread's deleteLater() share ONE finished() slot on
// purpose. A worker that posts its response itself answers before finished()
// is delivered, so a caller that stops pumping events once it has the answer
// never frees the QThread. McpTestFixture::callAsyncTool is such a caller on
// the last tool call of a test binary: LeakSanitizer, 836 bytes in
// tst_mcptools_shots_debuglog, nightly run 45419785997.
inline void mcpRespondFromWorker(std::function<QJsonObject()> work,
                                 std::function<void(QJsonObject)> respond)
{
    auto result = std::make_shared<QJsonObject>();
    QThread* thread = QThread::create([job = std::move(work), result]() {
        *result = job();
    });
    QObject::connect(thread, &QThread::finished, qApp,
                     [thread, result, reply = std::move(respond)]() {
        reply(*result);
        thread->deleteLater();
    });
    thread->start();
}
