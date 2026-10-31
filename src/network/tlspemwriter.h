#pragma once

#include "core/diagnosticlogging.h"

#include <QSaveFile>
#include <QString>
#include <openssl/bio.h>
#include <memory>

namespace TlsPemWriter {

// Memory BIOs avoid the Windows FILE*/OPENSSL_Applink dependency:
// openssl/openssl crypto/bio/bss_file.c:83-92 and ms/uplink.c:25-28 (3.5.4).
template<typename Encoder>
bool write(const QString& path, Encoder encode, QString* error)
{
    error->clear();
    const std::unique_ptr<BIO, decltype(&BIO_free)> buffer(BIO_new(BIO_s_mem()), BIO_free);
    if (!buffer || encode(buffer.get()) != 1) {
        *error = QStringLiteral("Cannot encode PEM in memory");
        return false;
    }
    char* bytes = nullptr;
    const long length = BIO_get_mem_data(buffer.get(), &bytes);
    if (length <= 0 || !bytes) {
        *error = QStringLiteral("PEM encoder returned no data");
        return false;
    }

    DIAG_DEBUG(NETWORK, "TlsPemWriter") << "Open Qt PEM output:" << path << "sizeBytes=" << length;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes, length) != length) {
        *error = output.errorString();
        return false;
    }
    DIAG_DEBUG(NETWORK, "TlsPemWriter") << "Commit Qt PEM output:" << path;
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}

} // namespace TlsPemWriter
