#pragma once

#include "remote/languageservice.h"

#include <QJSValue>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

// Read-only QML compatibility adapter. LanguageService supplies the canonical
// host catalog; only the native TranslationManager registers or writes strings.
class RemoteTranslationManager final : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(TranslationManager)

    Q_PROPERTY(LanguageService* languageService READ languageService WRITE setLanguageService NOTIFY languageServiceChanged FINAL)
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY translationsChanged FINAL)
    Q_PROPERTY(qint64 translationVersion READ translationVersion NOTIFY translationsChanged FINAL)
    Q_PROPERTY(QJSValue translate READ translateFn NOTIFY translationsChanged FINAL)
    Q_PROPERTY(bool catalogReady READ catalogReady NOTIFY catalogStatusChanged FINAL)
    Q_PROPERTY(QString catalogStatus READ catalogStatus NOTIFY catalogStatusChanged FINAL)

public:
    static RemoteTranslationManager* create(QQmlEngine* engine, QJSEngine* jsEngine);
    LanguageService* languageService() const { return m_languageService.data(); }
    void setLanguageService(LanguageService* service);
    QString currentLanguage() const { return m_language; }
    qint64 translationVersion() const { return m_translationVersion; }
    bool catalogReady() const { return m_catalogReady; }
    QString catalogStatus() const { return m_catalogStatus; }
    Q_INVOKABLE QString translateString(const QString& key, const QString& fallback) const;
    Q_INVOKABLE bool hasTranslation(const QString& key) const;
    Q_INVOKABLE void refreshCatalog();
    QJSValue translateFn();

signals:
    void languageServiceChanged();
    void translationsChanged();
    void catalogStatusChanged();

private:
    explicit RemoteTranslationManager(QObject* parent = nullptr);
    void updateForService();
    void requestPage();
    void completePage(const QString& requestId, const QString& operation, bool success,
                      const QVariantMap& result, const QString& reason);
    bool appendPage(const QVariantMap& result);
    void clearCatalog(const QString& status);
    void setCatalogStatus(const QString& status);
    void bumpTranslationVersion();

    QPointer<LanguageService> m_languageService;
    QMap<QString, QString> m_translations;
    QMap<QString, QString> m_pendingFallbacks;
    QMap<QString, QString> m_pendingTranslations;
    QString m_language;
    QString m_requestedLanguage;
    QString m_pendingRequestId;
    QString m_catalogStatus = QStringLiteral("disconnected");
    QJSValue m_translateFn;
    QJSEngine* m_jsEngine = nullptr;
    qint64 m_requestedRevision = -1;
    qint64 m_translationVersion = 0;
    qsizetype m_pendingTextUnits = 0;
    int m_expectedTotal = -1;
    bool m_catalogReady = false;
};

static_assert(!std::is_default_constructible_v<RemoteTranslationManager>,
              "The QML singleton must use its engine-owned factory.");
