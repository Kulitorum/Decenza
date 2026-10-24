#pragma once

#include "remoteservice.h"
#include "rep_languageservice_replica.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <type_traits>

class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;

// Shared QML command facade for native and paired browser clients. Catalog
// lookup remains TranslationManager's job; this service provides host-owned
// language and string-editor operations.
class LanguageService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool uploading READ uploading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged FINAL)
    Q_PROPERTY(int scanProgress READ scanProgress NOTIFY stateChanged FINAL)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool autoTranslating READ autoTranslating NOTIFY stateChanged FINAL)
    Q_PROPERTY(int autoTranslateProgress READ autoTranslateProgress NOTIFY stateChanged FINAL)
    Q_PROPERTY(int autoTranslateTotal READ autoTranslateTotal NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString retryStatus READ retryStatus NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString lastTranslatedText READ lastTranslatedText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList strings READ strings NOTIFY stringsChanged FINAL)
    Q_PROPERTY(QVariantMap stringsContext READ stringsContext NOTIFY stringsChanged FINAL)
    Q_PROPERTY(bool stringsLoading READ stringsLoading NOTIFY stringsChanged FINAL)
    Q_PROPERTY(int untranslatedCount READ untranslatedCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int totalStringCount READ totalStringCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int uniqueStringCount READ uniqueStringCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int uniqueUntranslatedCount READ uniqueUntranslatedCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canAutoTranslate READ canAutoTranslate NOTIFY stateChanged FINAL)
public:
    static LanguageService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    ~LanguageService() override;

    QVariantMap snapshot() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }
    QStringList availableLanguages() const;
    bool downloading() const;
    bool uploading() const;
    bool scanning() const;
    int scanProgress() const;
    int scanTotal() const;
    bool autoTranslating() const;
    int autoTranslateProgress() const;
    int autoTranslateTotal() const;
    QString retryStatus() const;
    QString lastTranslatedText() const;
    QVariantList strings() const { return m_strings; }
    QVariantMap stringsContext() const { return m_stringsContext; }
    bool stringsLoading() const { return m_stringsLoading; }
    int untranslatedCount() const;
    int totalStringCount() const;
    int uniqueStringCount() const;
    int uniqueUntranslatedCount() const;
    bool canAutoTranslate() const;
    Q_INVOKABLE QString getLanguageDisplayName(const QString& code) const;
    Q_INVOKABLE QString getLanguageNativeName(const QString& code) const;
    Q_INVOKABLE bool isRemoteLanguage(const QString& code) const;
    Q_INVOKABLE int getTranslationPercent(const QString& code) const;
    Q_INVOKABLE void scanAllStrings();
    Q_INVOKABLE void downloadLanguageList();
    Q_INVOKABLE void selectLanguage(const QString& code);
    Q_INVOKABLE void downloadLanguage(const QString& code);
    Q_INVOKABLE void addLanguage(const QString& code, const QString& displayName,
                                 const QString& nativeName);
    Q_INVOKABLE void deleteLanguage(const QString& code);
    Q_INVOKABLE void submitTranslation();
    Q_INVOKABLE void autoTranslate();
    Q_INVOKABLE void cancelAutoTranslate();
    Q_INVOKABLE void refreshStrings();
    Q_INVOKABLE QVariantMap currentContext() const;
    QString requestCatalogPage(qint64 revision, int offset);
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {});

signals:
    void stateChanged();
    void busyChanged();
    void stringsChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);

private:
    friend struct LanguageServiceForeign;
    explicit LanguageService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void finishPending(const QString& requestId, const QString& operation,
                       bool success, const QVariantMap& result,
                       const QString& reason);
    void failPending(const QString& reason);
    void requestStringPage(int offset);
    void observeConnectionState();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<LanguageStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_errorMessage;
    QVariantList m_strings;
    QVariantList m_loadingStrings;
    QVariantMap m_stringsContext;
    QVariantMap m_requestedStringsContext;
    int m_stringsTotal = 0;
    bool m_stringsLoading = false;
    bool m_stringsRequested = false;
    quint64 m_stringsGeneration = 0;
    int m_lastStringVersion = -1;
    QHash<QString, quint64> m_internalRequestGenerations;
    bool m_lastReady = false;
};

static_assert(!std::is_default_constructible_v<LanguageService>,
              "The QML singleton must use its engine-owned factory.");
