#pragma once

#include "core/diagnosticlogging.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QUrl>
#include <QUuid>
#include <memory>

// Diagnostic context only: never owns a request, schedules work, or changes a
// provider. Owners pass its opaque id through their existing callbacks. The weak
// index lets the local page reader hand off to AIManager during signal delivery;
// it retains neither completed requests nor response/prompt content.
class AIOperationLog {
public:
    using Ptr = std::shared_ptr<AIOperationLog>;

    static Ptr begin(const QString& kind, bool bag, qint64 bagId = 0, qint64 shotId = 0)
    {
        auto op = Ptr(new AIOperationLog(kind, bag, bagId, shotId));
        {
            QMutexLocker lock(&indexMutex());
            index().insert(op->id, op);
        }
        op->event(QtInfoMsg, QStringLiteral("start"));
        return op;
    }

    static Ptr find(const QString& id)
    {
        QMutexLocker lock(&indexMutex());
        return index().value(id).lock();
    }

    ~AIOperationLog()
    {
        finish(QStringLiteral("cancelled"), QStringLiteral("ownerDestroyed"));
        QMutexLocker lock(&indexMutex());
        index().remove(id);
    }

    static QString safeUrl(const QString& text)
    {
        QUrl url(text);
        if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
            || url.host().isEmpty())
            return QStringLiteral("invalid-or-non-http-url");
        url.setUserInfo(QString());
        url.setQuery(QString());
        url.setFragment(QString());
        // QUrl encodes control characters, so URL identity cannot create fields
        // or physical log lines. Never decode the result before writing it.
        return url.toString(QUrl::FullyEncoded).left(384);
    }

    static QString field(QString value)
    {
        // Provider/model identifiers, never remote prose. Keep one bounded field.
        for (auto& c : value) {
            if (!(c.isLetterOrNumber() || QStringLiteral("._-/:@").contains(c)))
                c = QLatin1Char('_');
        }
        return value.left(128);
    }

    void useProvider(const QString& providerId, const QString& modelId, const QString& nextStage)
    {
        provider = field(providerId);
        model = field(modelId);
        providerInvoked = true;
        stage = nextStage;
        event(QtDebugMsg, QStringLiteral("dispatch"));
    }

    void network(const QString& nextStage, const QString& url, int status, int error)
    {
        stage = nextStage;
        service = safeUrl(url);
        httpStatus = status;
        networkError = error;
        if (!providerInvoked && !pageHttpStatus)
            pageHttpStatus = status;
        event(QtDebugMsg, QStringLiteral("response"));
    }

    void detail(const QString& nextStage, const QString& reason)
    {
        stage = nextStage;
        event(QtDebugMsg, reason);
    }

    bool finish(const QString& outcome, const QString& reason, qsizetype count = -1)
    {
        if (terminal)
            return false;
        terminal = true;
        const bool fault = outcome == QLatin1String("failed") || outcome == QLatin1String("rejected");
        const QString result = QStringLiteral("terminal outcome=%1 reason=%2%3")
            .arg(field(outcome), field(reason), count < 0 ? QString()
                : QStringLiteral(" resultCount=%1").arg(count));
        event(fault ? QtWarningMsg : QtInfoMsg, result);
        return true;
    }

    QString fields() const
    {
        return QStringLiteral("op=%1 kind=%2 stage=%3 elapsedMs=%4 bagId=%5 shotId=%6 "
                              "provider=%7 model=%8 httpStatus=%9 networkError=%10 pageHttpStatus=%11%12")
            .arg(id, kind, stage).arg(timer.elapsed()).arg(bagId).arg(shotId)
            .arg(providerInvoked ? provider : QStringLiteral("not-invoked"), model)
            .arg(httpStatus).arg(networkError).arg(pageHttpStatus)
            .arg(service.isEmpty() ? QString() : QStringLiteral(" url=%1").arg(service));
    }

    QString owner() const
    {
        return QString::fromLatin1(bag ? DECENZA_LOG_MARKER_BEANBASE : DECENZA_LOG_MARKER_AI);
    }

    const QString id;
    const QString kind;
    const bool bag;
    const qint64 bagId;
    const qint64 shotId;
    QString stage = QStringLiteral("entry");
    QString provider;
    QString model;
    QString service;
    bool providerInvoked = false;
    bool terminal = false;
    int httpStatus = 0;
    int networkError = 0;
    int pageHttpStatus = 0;

private:
    AIOperationLog(const QString& kind, bool bag, qint64 bagId, qint64 shotId)
        : id(QUuid::createUuid().toString(QUuid::WithoutBraces)), kind(field(kind)),
          bag(bag), bagId(bagId), shotId(shotId)
    { timer.start(); }

    void event(QtMsgType type, const QString& text) const
    {
        const QString message = fields() + QLatin1Char(' ') + text;
        if (bag) {
            if (type == QtWarningMsg) { DIAG_WARN(BEANBASE, "Operation") << message; }
            else if (type == QtInfoMsg) { DIAG_INFO(BEANBASE, "Operation") << message; }
            else { DIAG_DEBUG(BEANBASE, "Operation") << message; }
        } else {
            if (type == QtWarningMsg) { DIAG_WARN(AI, "Operation") << message; }
            else if (type == QtInfoMsg) { DIAG_INFO(AI, "Operation") << message; }
            else { DIAG_DEBUG(AI, "Operation") << message; }
        }
    }

    static QHash<QString, std::weak_ptr<AIOperationLog>>& index()
    { static QHash<QString, std::weak_ptr<AIOperationLog>> value; return value; }
    static QMutex& indexMutex() { static QMutex value; return value; }
    QElapsedTimer timer;
};
