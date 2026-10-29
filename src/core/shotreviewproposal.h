#pragma once

#include "shotreviewbaseline.h"
#include <QVariantList>
#include <QVariantMap>

// Builds the candidate from edits made in this review, not from differences
// between an old shot and the live dial. The caller supplies the opening and
// last successfully saved values for this review session.
inline QVariantMap shotReviewProposal(const ShotReviewSetup& setup,
                                      const QVariantMap& opening,
                                      const QVariantMap& saved)
{
    QVariantMap patch;
    QVariantList changes;
    bool valid = true;
    const auto add = [&](const QString& key, const QVariant& current, bool number = false) {
        if (!opening.contains(key) || !saved.contains(key)) { valid = false; return; }
        const QVariant beforeEdit = opening.value(key);
        const QVariant afterEdit = saved.value(key);
        if (number) {
            bool beforeOk = false, afterOk = false;
            double before = beforeEdit.toDouble(&beforeOk);
            double after = afterEdit.toDouble(&afterOk);
            double live = current.toDouble();
            if (!beforeOk || !afterOk || !std::isfinite(before) || !std::isfinite(after)) {
                valid = false;
                return;
            }
            if (key == QLatin1String("bagId") || key == QLatin1String("equipmentId")) {
                if (std::floor(before) != before || std::floor(after) != after
                    || before > 9007199254740991.0 || after > 9007199254740991.0) {
                    valid = false;
                    return;
                }
                before = before > 0 ? before : -1;
                after = after > 0 ? after : -1;
                live = live > 0 ? live : -1;
            } else if (before < 0 || after < 0
                       || (key == QLatin1String("rpm")
                           && (after > 20000 || std::floor(after) != after))) {
                valid = false;
                return;
            }
            if (std::abs(before - after) < 0.005 || std::abs(live - after) < 0.005) return;
        } else {
            if (beforeEdit.metaType().id() != QMetaType::QString
                || afterEdit.metaType().id() != QMetaType::QString
                || afterEdit.toString().size() > 262144) {
                valid = false;
                return;
            }
            if (beforeEdit == afterEdit || current == afterEdit) return;
        }
        patch.insert(key, afterEdit);
        changes.append(QVariantMap{{QStringLiteral("field"), key},
                                   {QStringLiteral("before"), current},
                                   {QStringLiteral("after"), afterEdit}});
    };
    add(QStringLiteral("bagId"), setup.brew.activeBagId > 0 ? setup.brew.activeBagId : -1, true);
    add(QStringLiteral("beanBrand"), setup.beanBrand);
    add(QStringLiteral("beanType"), setup.beanType);
    add(QStringLiteral("roastDate"), setup.roastDate);
    add(QStringLiteral("roastLevel"), setup.roastLevel);
    add(QStringLiteral("beanBaseId"), setup.beanBaseId);
    add(QStringLiteral("beanBaseJson"), setup.beanBaseData);
    add(QStringLiteral("doseWeight"), setup.brew.doseG, true);
    add(QStringLiteral("grinderSetting"), setup.brew.grindSetting);
    add(QStringLiteral("rpm"), setup.brew.grindRpm, true);
    add(QStringLiteral("equipmentId"), setup.brew.activeEquipmentId > 0 ? setup.brew.activeEquipmentId : -1, true);
    add(QStringLiteral("barista"), setup.barista);

    if (patch.contains(QStringLiteral("doseWeight"))
        && YieldSpec::normalizedMode(setup.brew.yieldMode) == YieldSpec::modeRatio()) {
        const double target = YieldSpec::resolveGrams(setup.brew.yieldMode,
            setup.brew.yieldValue, patch.value(QStringLiteral("doseWeight")).toDouble(),
            setup.brew.profileTargetWeightG);
        if (std::abs(target - setup.targetG) >= 0.005)
            changes.append(QVariantMap{{QStringLiteral("field"), QStringLiteral("targetWeight")},
                                       {QStringLiteral("before"), setup.targetG},
                                       {QStringLiteral("after"), target}});
    }
    if (!valid) return {{QStringLiteral("valid"), false}};
    return {{QStringLiteral("valid"), true}, {QStringLiteral("patch"), patch},
            {QStringLiteral("changes"), changes}};
}
