#pragma once

#include "mcp/mcplogfilter.h"

#include <QJSEngine>
#include <QHash>
#include <QObject>
#include <QMetaObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;

class SystemLogViewTestService final : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SystemLogService)
    QML_SINGLETON
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(int activeViewCount READ activeViewCount NOTIFY testMetricsChanged)
    Q_PROPERTY(int snapshotCount READ snapshotCount NOTIFY testMetricsChanged)
    Q_PROPERTY(int boundaryCount READ boundaryCount NOTIFY testMetricsChanged)
    Q_PROPERTY(QString lastSnapshotRequestId READ lastSnapshotRequestId NOTIFY testMetricsChanged)
    Q_PROPERTY(QString lastBoundaryRequestId READ lastBoundaryRequestId NOTIFY testMetricsChanged)

public:
    static SystemLogViewTestService* create(QQmlEngine*, QJSEngine*)
    {
        static auto* instance = new SystemLogViewTestService;
        QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }

    bool ready() const { return true; }
    int activeViewCount() const { return static_cast<int>(m_activeViews.size()); }
    int snapshotCount() const { return m_snapshotCount; }
    int boundaryCount() const { return m_boundaryCount; }
    QString lastSnapshotRequestId() const { return m_lastSnapshotRequestId; }
    QString lastBoundaryRequestId() const { return m_lastBoundaryRequestId; }

    Q_INVOKABLE void setViewActive(QObject* view, bool active)
    {
        if (!view)
            return;
        const auto it = m_activeViews.find(view);
        if (active) {
            if (it != m_activeViews.end())
                return;
            const auto connection = connect(view, &QObject::destroyed, this, [this, view] {
                m_activeViews.remove(view);
                emit stateChanged();
                emit testMetricsChanged();
            });
            m_activeViews.insert(view, connection);
        } else {
            if (it == m_activeViews.end())
                return;
            disconnect(it.value());
            m_activeViews.erase(it);
        }
        emit stateChanged();
        emit testMetricsChanged();
    }

    Q_INVOKABLE QString requestSnapshot(const QStringList&, const QString&, int)
    {
        m_lastSnapshotRequestId = QStringLiteral("snapshot-%1").arg(++m_snapshotCount);
        emit testMetricsChanged();
        return m_lastSnapshotRequestId;
    }

    Q_INVOKABLE QString requestBoundary()
    {
        m_lastBoundaryRequestId = QStringLiteral("boundary-%1").arg(++m_boundaryCount);
        emit testMetricsChanged();
        return m_lastBoundaryRequestId;
    }

    Q_INVOKABLE bool lineMatches(const QString& line, const QStringList& markers,
                                 const QString& minLevel) const
    {
        return McpLogFilter::lineMatches(line, markers, minLevel);
    }

    Q_INVOKABLE void completeSnapshot(const QString& requestId, bool success,
                                      const QStringList& lines, qint64 sequence,
                                      const QString& reason)
    {
        emit snapshotCompleted(requestId, success, lines, sequence, reason);
    }

    Q_INVOKABLE void completeBoundary(const QString& requestId, bool success,
                                      qint64 sequence, const QString& reason)
    {
        emit boundaryCompleted(requestId, success, sequence, reason);
    }

    Q_INVOKABLE void appendLine(const QString& line, qint64 sequence)
    {
        emit lineAppended(line, sequence);
    }

    Q_INVOKABLE void resetForTest()
    {
        m_snapshotCount = 0;
        m_boundaryCount = 0;
        m_lastSnapshotRequestId.clear();
        m_lastBoundaryRequestId.clear();
        emit testMetricsChanged();
    }

signals:
    void stateChanged();
    void testMetricsChanged();
    void snapshotCompleted(const QString& requestId, bool success,
                           const QStringList& lines, qint64 sequence,
                           const QString& reason);
    void boundaryCompleted(const QString& requestId, bool success,
                           qint64 sequence, const QString& reason);
    void lineAppended(const QString& line, qint64 sequence);

private:
    SystemLogViewTestService() = default;

    QHash<QObject*, QMetaObject::Connection> m_activeViews;
    int m_snapshotCount = 0;
    int m_boundaryCount = 0;
    QString m_lastSnapshotRequestId;
    QString m_lastBoundaryRequestId;
};
