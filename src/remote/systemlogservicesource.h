#pragma once

#include "rep_systemlogservice_source.h"

#include <QPointer>
#include <QStringList>

class WebDebugLogger;

// Host-owned persisted log reader and one raw live log stream. Snapshot reads run
// on the logger's host thread and are capped before they cross Qt Remote Objects.
class NativeSystemLogServiceSource final : public SystemLogStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeSystemLogServiceSource(WebDebugLogger* logger,
                                          QObject* parent = nullptr);

    void requestSnapshot(QString requestId, QStringList markers, QString minLevel,
                         int maxLines) override;
    void requestBoundary(QString requestId) override;

private:
    static bool validRequestId(const QString& requestId);
    static QString validateFilter(const QStringList& markers,
                                  const QString& minLevel, int maxLines);
    void onLineAppended(const QString& line, qint64 sequence);

    QPointer<WebDebugLogger> m_logger;
};
