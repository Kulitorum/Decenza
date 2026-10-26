#pragma once

#include "remoteservice.h"
#include "rep_libraryservice_replica.h"

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QImage>

class QRemoteObjectNode;
class QQmlEngine;
class WidgetLibrary;
class QNetworkAccessManager;

class LibraryService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList localEntries READ localEntries NOTIFY stateChanged FINAL)
    Q_PROPERTY(quint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~LibraryService() override;
    QVariantList localEntries() const;
    quint64 revision() const;
    bool ready() const;
    QString errorMessage() const;
    bool busy() const { return !m_pending.isEmpty(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static void setPublishedLocalLibrary(WidgetLibrary* library);
    static LibraryService* create(QQmlEngine* engine);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& arguments,
                                const QString& requestId = QString());
    Q_INVOKABLE QString thumbnailUrl(const QString& entryId, bool compact = false) const;
    Q_INVOKABLE bool isEmptyThemePreview(const QImage& image) const;
    Q_INVOKABLE int checkThemePreview(const QString& source);

signals:
    void stateChanged();
    void busyChanged();
    void themePreviewChecked(const QString& source, bool empty);
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct LibraryServiceForeign;
    explicit LibraryService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void failPending(const QString& reason);
    void changed();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<LibraryStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QNetworkAccessManager* m_previewNetwork = nullptr;
    QHash<QString, bool> m_themePreviewResults;
    QSet<QString> m_pendingThemePreviews;
};
