#pragma once

#include <QByteArray>
#include <QRandomGenerator>
#include <QString>

// The capability tokens that authorize remote access (the MCP connector URL and
// the Remote Web Access URL). One generator so both routes keep the same
// strength and alphabet.
namespace CapabilityToken {

// 128 bits from the OS CSPRNG, base64url without padding: a 22-char path segment.
inline QString generate()
{
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    const QByteArray raw(reinterpret_cast<const char*>(words), sizeof(words));
    return QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// Constant time over the token; a length mismatch returns early, which leaks
// nothing because every token has the same fixed length.
inline bool matches(const QByteArray& candidate, const QByteArray& token)
{
    if (token.isEmpty() || candidate.size() != token.size())
        return false;
    quint8 diff = 0;
    for (qsizetype i = 0; i < token.size(); ++i)
        diff |= static_cast<quint8>(candidate[i] ^ token[i]);
    return diff == 0;
}

} // namespace CapabilityToken
