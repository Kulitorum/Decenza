#pragma once

#include <QRegularExpression>
#include <QString>

namespace AccessibilityText {

// Pure text formatting shared by native speech and browser accessibility labels.
inline QString cleanForSpeech(const QString& text)
{
    if (text.isEmpty())
        return QString();

    // Keep the established expansion order: unit replacements insert spaces,
    // which the final whitespace collapse normalizes.
    static const QRegularExpression profileExt(QStringLiteral("\\.(json|tcl|txt)$"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression separators(QStringLiteral("[_-]"));
    // These symbols and a bare "88C" in profile names always denote their own
    // unit. Native converted readings emit explicit °F/°C, so this cannot
    // mislabel a converted value.
    static const QRegularExpression bareCelsius(QStringLiteral("(\\d)\\s*C\\b"));
    static const QRegularExpression millilitres(QStringLiteral("(\\d)\\s*ml\\b"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression grams(QStringLiteral("(\\d)\\s*g\\b"));
    static const QRegularExpression bar(QStringLiteral("(\\d)\\s*bar\\b"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression seconds(QStringLiteral("(\\d)\\s*s\\b"));
    static const QRegularExpression percent(QStringLiteral("(\\d)\\s*%"));
    static const QRegularExpression runsOfSpace(QStringLiteral("\\s+"));

    QString cleaned = text;
    cleaned.remove(profileExt);
    cleaned.replace(separators, QStringLiteral(" "));
    cleaned.replace(QStringLiteral("°F"), QStringLiteral(" degrees Fahrenheit"));
    cleaned.replace(QStringLiteral("°C"), QStringLiteral(" degrees Celsius"));
    cleaned.replace(bareCelsius, QStringLiteral("\\1 degrees Celsius"));
    cleaned.replace(millilitres, QStringLiteral("\\1 milliliters"));
    cleaned.replace(grams, QStringLiteral("\\1 grams"));
    cleaned.replace(bar, QStringLiteral("\\1 bar"));
    cleaned.replace(seconds, QStringLiteral("\\1 seconds"));
    cleaned.replace(percent, QStringLiteral("\\1 percent"));
    cleaned.replace(runsOfSpace, QStringLiteral(" "));
    return cleaned.trimmed();
}

} // namespace AccessibilityText
