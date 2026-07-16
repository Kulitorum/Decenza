#pragma once

#include <QJsonObject>
#include <QString>
#include <QLatin1String>

// add-ai-taste-intake: map the coarse taste taps onto Visualizer's SCA CVA
// descriptive attributes (intensity 0-15). Extracted as a pure, dependency-free
// function so the value table and the never-null invariant are unit-testable
// (tests/tst_tastecvamap.cpp) without standing up a VisualizerUploader.
//
// Contract:
//   - A field is written ONLY when its tap is set (in-set value) — and NEVER as
//     null — so an untapped shot never clears a CVA value the user scored by hand
//     in Visualizer's assessment form.
//   - The five CVA attributes the taps don't speak to (sweetness, aftertaste,
//     aroma, flavor, fragrance) are never touched.
inline void applyTasteCvaMapping(QJsonObject& obj,
                                 const QString& tasteBalance,
                                 const QString& tasteBody)
{
    if (tasteBalance == QLatin1String("sour"))          { obj["acidity"] = 12; obj["bitterness"] = 4;  }
    else if (tasteBalance == QLatin1String("balanced")) { obj["acidity"] = 8;  obj["bitterness"] = 8;  }
    else if (tasteBalance == QLatin1String("bitter"))   { obj["acidity"] = 4;  obj["bitterness"] = 12; }

    if (tasteBody == QLatin1String("thin"))        obj["mouthfeel"] = 4;
    else if (tasteBody == QLatin1String("medium")) obj["mouthfeel"] = 8;
    else if (tasteBody == QLatin1String("heavy"))  obj["mouthfeel"] = 12;
}

// Inverse of applyTasteCvaMapping: reconstruct the coarse taps from a
// Visualizer shot's CVA attributes (as recovered via /api/shots/{id}/download).
// Used by the recovery importer so a shot's taste dial-in survives the
// upload→download round-trip.
//
// A user may also score these 0-15 by hand in Visualizer's assessment form
// (not just Decenza's discrete 4/8/12), so we bucket by intensity rather than
// exact-match, and treat "both unset" / "unset" (0) as no tap — returning "" so
// an untapped or hand-unscored shot doesn't invent a taste value.
inline QString cvaToTasteBalance(int acidity, int bitterness)
{
    if (acidity <= 0 && bitterness <= 0) return QString();  // unset
    if (acidity > bitterness)  return QStringLiteral("sour");
    if (bitterness > acidity)  return QStringLiteral("bitter");
    return QStringLiteral("balanced");                      // equal & non-zero
}

inline QString cvaToTasteBody(int mouthfeel)
{
    if (mouthfeel <= 0) return QString();                   // unset
    if (mouthfeel <= 5) return QStringLiteral("thin");      // 1-5  (maps 4)
    if (mouthfeel <= 9) return QStringLiteral("medium");    // 6-9  (maps 8)
    return QStringLiteral("heavy");                         // 10-15 (maps 12)
}
