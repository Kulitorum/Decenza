#pragma once

#include "rep_recipecontextservice_source.h"
#include <QPointer>
#include <QHash>

class Settings;
class ShotHistoryStorage;

class NativeRecipeContextSource final : public RecipeContextStateSimpleSource
{
    Q_OBJECT
public:
    NativeRecipeContextSource(Settings* settings, ShotHistoryStorage* history,
                              QObject* parent = nullptr);

    void refresh() override;
    void setPreference(QString requestId, QString name, QVariant value,
                       quint64 expectedRevision) override;
    void executePresetEdit(QString requestId, QString domain, QString operation,
                           QVariantMap fields, quint64 expectedRevision) override;
    void applyShotBaseline(QString requestId, QVariantMap fields,
                           quint64 expectedRevision) override;
    void requestHistory(QString requestId, QString operation,
                        QVariantMap fields) override;
    void requestShotMetadataUpdate(QString requestId, qint64 shotId,
                                   QVariantMap metadata) override;

signals:
    void presetApplyRequested(const QString& domain);

private:
    void finishHistory(const QString& requestId, const QString& operation,
                       bool success, const QVariantMap& result, const QString& reason);
    void finishGrindContext(const QString& requestId, const QVariantMap& history,
                            const QString& reason);
    bool validRequestId(const QString& requestId) const;
    QVariantMap capturePreferences() const;

    QPointer<Settings> m_settings;
    QPointer<ShotHistoryStorage> m_history;
    quint64 m_revision = 0;
    bool m_suppressPreferenceRefresh = false;
    QHash<QString, QString> m_pendingRequestIds;
    QHash<QString, QVariantMap> m_pendingGrindFields;
};
