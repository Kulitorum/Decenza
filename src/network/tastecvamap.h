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
