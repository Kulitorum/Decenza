#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Small source-independent validation seam for ShotUploadService. This only
// accepts commands; all effects still go through the existing host controllers.
namespace ShotUploadCommandPolicy {

struct Command {
    QString operation;
    QVariantMap arguments;
};

bool decode(const QString& operation, const QVariantMap& arguments,
            Command* command, QString* reason = nullptr);
bool validCompletionOwner(const QString& requestId, const QString& operation,
                          const QString& expectedId, const QString& expectedOperation);

} // namespace ShotUploadCommandPolicy
