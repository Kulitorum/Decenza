#pragma once

#include <QCryptographicHash>
#include <QString>

/**
 * The derivation of an AI conversation's storage key, in one place.
 *
 * Two producers need it and they are not in the same layer: AIManager derives
 * it from the shot under review, and AIConversation::importConversationsStatic
 * re-derives it for an incoming conversation whose equipment package was
 * renumbered by the import. A second copy of this hash would let a restored
 * thread key differently from the thread the same shot opens, which is a
 * silent orphan — the index entry is there, the shot never reaches it.
 *
 * The package is part of the identity because a saved thread REPLAYS its
 * stored turns on every request; scoping the payload alone leaves older turns
 * describing another basket inside the same transcript, still informing every
 * answer. `equipmentId` 0 is the unpackaged pool, matching AdviceScope.
 */
namespace ConversationKey {

inline QString derive(const QString& beanBrand, const QString& beanType,
                      const QString& profileName, qint64 equipmentId)
{
    const QString normalized = beanBrand.toLower().trimmed() + "|" +
                               beanType.toLower().trimmed() + "|" +
                               profileName.toLower().trimmed() + "|" +
                               QString::number(equipmentId);
    return QCryptographicHash::hash(normalized.toUtf8(),
                                    QCryptographicHash::Sha1).toHex().left(16);
}

}  // namespace ConversationKey
