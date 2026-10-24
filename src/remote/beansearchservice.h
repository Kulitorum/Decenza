#pragma once
#include "remoteservice.h"

#include "rep_beansearchservice_replica.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;

class BeanSearchService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString bagKind READ bagKind WRITE setBagKind NOTIFY bagKindChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged FINAL)

public:
    ~BeanSearchService() override;

    QVariantList results() const { return m_results; }
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    QString bagKind() const { return m_bagKind; }
    void setBagKind(const QString& bagKind);
    bool searching() const { return m_searching; }
    QString errorMessage() const { return m_errorMessage; }
    bool ready() const { return connected(); }
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE void refresh();

signals:
    void readyChanged();
    void resultsChanged();
    void queryChanged();
    void bagKindChanged();
    void searchingChanged();
    void errorMessageChanged();

private:
    friend struct BeanSearchServiceForeign;
    explicit BeanSearchService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void startSearch();
    void setSearching(bool searching);
    void setErrorMessage(const QString& message);
    void failPending();
    void updateConnectionState();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<BeanSearchStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QVariantList m_results;
    QString m_query;
    QString m_bagKind;
    QString m_requestId;
    QString m_errorMessage;
    bool m_searching = false;
    bool m_searchRequested = false;
    bool m_wasConnected = false;
};
