#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

// Narrow bridge to the browser's same-document history. The QML shell owns
// route policy; this class only preserves opaque, validated route-state JSON
// and reports browser popstate events.
class BrowserHistoryController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static BrowserHistoryController* create(QQmlEngine* engine, QJSEngine* jsEngine);
    ~BrowserHistoryController() override;

    Q_INVOKABLE QString currentStateJson() const;
    Q_INVOKABLE QString currentLocationHash() const;
    Q_INVOKABLE QString newSessionId() const;
    Q_INVOKABLE void replaceState(const QString& stateJson, const QString& routeHash);
    Q_INVOKABLE void pushState(const QString& stateJson, const QString& routeHash);
    Q_INVOKABLE void goFromApp(int delta, const QString& expectedSessionId, int expectedIndex);
    Q_INVOKABLE void restoreAfterPop(int delta, const QString& expectedStateJson,
                                     const QString& expectedRouteHash);
    Q_INVOKABLE void commitRestoredPop(int delta, const QString& expectedStateJson,
                                       const QString& expectedRouteHash);
    Q_INVOKABLE void cancelRestoredPop();
    Q_INVOKABLE void setBeforeUnloadRequired(bool required);
    static void receivePopStateFromBrowser(const QString& stateJson);
    static void receiveHashChangeFromBrowser(const QString& hash, const QString& stateJson);

signals:
    void popStateRequested(const QString& stateJson);
    void hashChangeRequested(const QString& hash, const QString& stateJson);
    void positionRestored();
    void popStateCommitted(const QString& stateJson);
    void appBackCompleted(const QString& stateJson);

private:
    explicit BrowserHistoryController(QObject* parent = nullptr);
    static QPointer<BrowserHistoryController> s_instance;

    bool m_restorePending = false;
    bool m_popRestored = false;
    bool m_commitPending = false;
    bool m_appBackPending = false;
    QString m_appBackExpectedSessionId;
    int m_appBackExpectedIndex = -1;
    QString m_expectedStateJson;
    QString m_expectedRouteHash;
};

static_assert(!std::is_default_constructible_v<BrowserHistoryController>,
              "QML must use the history controller factory");
