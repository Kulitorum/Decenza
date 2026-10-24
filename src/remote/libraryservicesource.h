#pragma once

#include "rep_libraryservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QHash>

class LibrarySharing;
class Settings;
class WidgetLibrary;

class NativeLibraryServiceSource final : public LibraryStateSimpleSource
{
    Q_OBJECT
public:
    NativeLibraryServiceSource(WidgetLibrary* library, LibrarySharing* sharing,
                               Settings* settings, QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    void publishLocalEntries(bool advanceRevision);
    void finish(const QString& requestId, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void completeSharing(const QString& requestId, bool success,
                         const QVariantMap& result, const QString& reason);
    QVariantList annotateOwnership(const QVariantList& entries) const;
    bool validArguments(const QString& operation, const QVariantMap& arguments) const;
    bool expectedLayoutMatches(const QVariantMap& arguments) const;

    QPointer<WidgetLibrary> m_library;
    QPointer<LibrarySharing> m_sharing;
    QPointer<Settings> m_settings;
    ServiceRequestIds m_requests;
    QHash<QString, QString> m_operations;
    quint64 m_revision = 0;
};
