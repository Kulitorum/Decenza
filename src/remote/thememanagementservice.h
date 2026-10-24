#pragma once

#include "remoteservice.h"
#include "rep_thememanagementservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

class QRemoteObjectNode;
class QQmlEngine;

class ThemeManagementService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap activeColors READ activeColors NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap palettes READ palettes NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString activeThemeName READ activeThemeName NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString themeMode READ themeMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList themeNames READ themeNames NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString darkThemeName READ darkThemeName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString lightThemeName READ lightThemeName NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool glassChrome READ glassChrome NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString backgroundPreset READ backgroundPreset NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString backgroundSource READ backgroundSource NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString backgroundPattern READ backgroundPattern NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool backgroundShotAdvanced READ backgroundShotAdvanced NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList backgroundImages READ backgroundImages NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool backgroundImageCacheEnabled READ backgroundImageCacheEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString backgroundImageId READ backgroundImageId NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList backgroundPresets READ backgroundPresets CONSTANT FINAL)
    Q_PROPERTY(QVariantList backgroundPatterns READ backgroundPatterns CONSTANT FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~ThemeManagementService() override;
    QVariantList presets() const;
    QVariantMap activeColors() const;
    QVariantMap palettes() const;
    QString activeThemeName() const;
    bool darkMode() const;
    qint64 revision() const;
    QString themeMode() const;
    QStringList themeNames() const;
    QString darkThemeName() const;
    QString lightThemeName() const;
    bool glassChrome() const;
    QString backgroundPreset() const;
    QString backgroundSource() const;
    QString backgroundPattern() const;
    bool backgroundShotAdvanced() const;
    QVariantList backgroundImages() const;
    bool backgroundImageCacheEnabled() const;
    QString backgroundImageId() const;
    QVariantList backgroundPresets() const;
    QVariantList backgroundPatterns() const;
    bool ready() const;
    QString errorMessage() const;
    bool busy() const { return !m_pending.isEmpty(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static ThemeManagementService* create(QQmlEngine* engine);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString execute(const QString& operation, const QString& name,
                                qint64 expectedRevision, const QString& requestId = QString());
    Q_INVOKABLE QString setPaletteColor(const QString& palette, const QString& colorName,
                                       const QString& colorValue, qint64 expectedRevision,
                                       const QString& requestId = QString());
    Q_INVOKABLE QString executeSetting(const QString& operation, const QVariantMap& arguments,
                                       qint64 expectedRevision,
                                       const QString& requestId = QString());

signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, qint64 revision, const QVariantMap& result,
                            const QString& reason);

private:
    friend struct ThemeManagementServiceForeign;
    explicit ThemeManagementService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ThemeManagementStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};
