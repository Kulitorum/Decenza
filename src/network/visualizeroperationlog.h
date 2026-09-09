#pragma once

#include "core/diagnosticlogging.h"
#include "core/logfields.h"
#include <QElapsedTimer>
#include <QMap>
#include <QNetworkReply>
#include <QUuid>
#include <memory>

// Callback-owned diagnostics only. Never retains replies, payloads or UI state,
// and never schedules, cancels or changes an application operation. Methods run
// on the owning object's thread; worker results are delivered back to that thread.
class VisualizerOperationLog {
public:
    using Ptr = std::shared_ptr<VisualizerOperationLog>;
    enum class Emitter { Uploader, Importer };

    static Ptr begin(Emitter emitter, const QString& kind, qint64 shotId = 0,
                     qint64 bagId = 0, const QString& remoteId = {}, const Ptr& parent = {})
    {
        auto op = Ptr(new VisualizerOperationLog(emitter, kind));
        op->set("shotId", shotId);
        op->set("bagId", bagId);
        if (!remoteId.isEmpty()) op->set("remoteId", remoteId);
        if (parent) op->set("parentOp", parent->id);
        op->event(QtInfoMsg, "start");
        return op;
    }

    ~VisualizerOperationLog() { finish("cancelled", "ownerDestroyed"); }

    void set(const QString& key, const QString& value)
    {
        if (!terminal) context.insert(DecenzaLog::field(key), DecenzaLog::field(value));
    }
    void set(const QString& key, qint64 value) { set(key, QString::number(value)); }

    void detail(const QString& nextStage, const QString& reason)
    {
        if (terminal) return;
        stage = DecenzaLog::field(nextStage);
        event(QtDebugMsg, QStringLiteral("detail reason=%1").arg(DecenzaLog::field(reason)));
    }

    void response(const QString& nextStage, QNetworkReply* reply)
    {
        if (terminal) return;
        stage = DecenzaLog::field(nextStage);
        set("httpStatus", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        set("networkError", reply->error());
        url = DecenzaLog::safeUrl(reply->url().toString());
        event(QtDebugMsg, "response");
    }

    // An item/stage problem is distinct from the terminal batch summary. Keep a
    // diagnostic count because existing UI counters sometimes classify it as a skip.
    void problem(const QString& nextStage, const QString& reason)
    {
        if (terminal) return;
        ++problems;
        stage = DecenzaLog::field(nextStage);
        event(QtWarningMsg, QStringLiteral("problem reason=%1").arg(DecenzaLog::field(reason)));
    }

    bool finish(const QString& outcome, const QString& reason)
    {
        if (terminal) return false;
        terminal = true;
        const bool fault = outcome == QLatin1String("failed") || outcome == QLatin1String("rejected")
            || outcome == QLatin1String("partial");
        event(fault ? QtWarningMsg : QtInfoMsg,
              QStringLiteral("terminal outcome=%1 reason=%2")
                  .arg(DecenzaLog::field(outcome), DecenzaLog::field(reason)));
        return true;
    }

    void finishBatch(qint64 total, qint64 imported, qint64 skipped, qint64 failed)
    {
        set("total", total); set("imported", imported); set("skipped", skipped); set("failed", failed);
        set("diagnosticFailures", problems);
        finish(failed > 0 || problems > 0 ? "partial" : total == 0 ? "empty" : "success", "batchComplete");
    }

    const QString id;
    const QString kind;
    QString stage = "entry";
    bool terminal = false;
    qint64 problems = 0;

private:
    VisualizerOperationLog(Emitter emitter, const QString& kind)
        : id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
          kind(DecenzaLog::field(kind)), emitter(emitter) { timer.start(); }

    void event(QtMsgType level, const QString& text) const
    {
        QString message = QStringLiteral("op=%1 kind=%2 stage=%3 elapsedMs=%4")
            .arg(id, kind, stage).arg(timer.elapsed());
        for (auto it = context.cbegin(); it != context.cend(); ++it)
            message += QLatin1Char(' ') + it.key() + QLatin1Char('=') + it.value();
        if (!url.isEmpty()) message += QStringLiteral(" url=") + url;
        message += QLatin1Char(' ') + text;
        if (emitter == Emitter::Uploader) {
            if (level == QtWarningMsg) { DIAG_WARN(VISUALIZER, "VisualizerUploader") << message; }
            else if (level == QtInfoMsg) { DIAG_INFO(VISUALIZER, "VisualizerUploader") << message; }
            else { DIAG_DEBUG(VISUALIZER, "VisualizerUploader") << message; }
        } else {
            if (level == QtWarningMsg) { DIAG_WARN(VISUALIZER, "VisualizerImporter") << message; }
            else if (level == QtInfoMsg) { DIAG_INFO(VISUALIZER, "VisualizerImporter") << message; }
            else { DIAG_DEBUG(VISUALIZER, "VisualizerImporter") << message; }
        }
    }

    const Emitter emitter;
    QElapsedTimer timer;
    QMap<QString, QString> context;
    QString url;
};
