# Espresso Dial-In Reference Tables

Source: Åbn Coffee — "Espresso tabel oversigt" (espresso table overview).
Work-in-progress community reference for dialing in espresso on profiling machines.

This document captures structured relationships between espresso variables (roast level, temperature, grind size, infusion, pressure, flow rate, ratio) and their effects on taste, texture, and extraction. Intended for integration into the AI advisor's system prompt to enable more specific, multi-variable dial-in recommendations.

---

## 1. Roast Level Effects

| Attribute | LIGHT | MEDIUM-LIGHT | MEDIUM | MEDIUM-DARK | DARK |
|-----------|-------|--------------|--------|-------------|------|
| **Acidity** | High | ← | | → | Low |
| **Clarity & origin taste** | High | ← | | → | Low |
| **Extractability + Mouthfeel & Body** | Low | ← | | → | High |
| **Risk of puck issues** | High | ← | | → | Low |
| **Puck prep importance** | Important | ← | | → | Not so important |
| **Characteristic taste** | Fruits | Caramel | Choc. + fruit | Milk choc. | Dark choc. |
| **Temperature (starting point)** | 96°C | 94°C | 92°C | 90°C | 88°C |

---

## 2. Temperature Effects

| Attribute | Higher temp (96°C) | | Lower temp (88°C) |
|-----------|-------------------|---|-------------------|
| **Risk of harshness** | Higher | → | Lower |
| **Texture** | Lower | → | Higher |
| **Extraction potential** | Highest | → | Lower |

---

## 3. Grind Size Effects

| Attribute | Too Fine | Fine | | | Coarse |
|-----------|----------|------|---|---|--------|
| **Risk of uneven extraction & channelling** | Very high | Higher | | | Lower |
| **Body / sweetness** | Less | ← | More | → | Less |
| **Clarity** | Too low | Lower | | | Higher |
| **Flow & reaching flow limit** | Flow below limit | Lower | | | Higher |
| **Pressure & reaching press limit** | Higher with probable constant pressure | | | Lower with probable pressure decline |
| **Extraction potential** | Less | ← | More | → | Less |

Note: Both too-fine and too-coarse produce less extraction — the sweet spot is in the middle. Too fine causes channelling (uneven extraction), too coarse causes under-extraction.

---

## 4. Infusion Phase (Preinfusion) Tuning

| Attribute | Longer infusion | | Shorter infusion |
|-----------|----------------|---|-----------------|
| **Drip weight (move-on threshold)** | 4–8 (10) g | | 0.5 g |
| **Move-on time** | 30 sec | | 5 sec |
| **Body** | More | → | Less |
| **Puck resilience & pressure @ pour** | Lower | → | Higher |
| **Texture / mouthfeel** | Thick / syrupy | → | Wine-like |
| **Clarity & acidity** | Lower | → | Higher |
| **Extraction potential** | Higher | → | Lower |

---

## 5. Pour Phase — Peak Pressure

| Attribute | Too low | 6 bar | 7–8.5–9 bar | Too high |
|-----------|---------|-------|-------------|----------|
| **Taste / clarity** | Thin, watery | Clear delicate flavors | Rich traditional flavors | Muddy cup lacking clarity |
| **Texture** | — | Wine-like | Syrupy & creamy | — |

Note: "Too high" also increases risk of bad-bean harsh taste becoming apparent.

---

## 6. Pour Phase — Flow Rate

| Attribute | High (2.5–4+ ml/s) | Mid (1.5–2.5 ml/s) | Low (1.0–1.5 ml/s) |
|-----------|---------------------|---------------------|---------------------|
| **Flavors favored** | Florals & fruits | | Chocolate, nuts, sugars & breads |
| **Mouthfeel / texture** | Less | | More |
| **Clarity / acidity** | More | | Less |
| **Extraction potential** | More | | Less |

### Flow Rate by Roast Level

| Roast | Recommendation |
|-------|---------------|
| Light / Medium-light | **Faster flow** — can increase clarity in lighter roasts |
| Medium-dark / Dark | **Slower flow** — can increase intensity and body in darker roasts |

### Flow Rate Use Cases

| Flow range | Suitable for |
|------------|-------------|
| < 0.5 ml/s | Bitter unpleasant taste (avoid) |
| 1.0–1.5 ml/s | Milk drinks |
| 1.5–2.5 ml/s | Straight espresso |
| 2.5–4+ ml/s | High-clarity espresso, light roasts |

### Flow/Pressure at Roast Level (Bimodal Grinder)

| Setting | Result |
|---------|--------|
| 1.7 ml/s / 7–9 bar | Thick syrupy taste |
| 2.2 ml/s / 6–7 bar | Lower body, more clarity and origin taste |

---

## 7. Ratio (dose:yield, shown as 1:X in shot data)

| Attribute | Too long (1:4+) | Long (1:3–1:4) | Standard (1:2–1:3) | Short (1:1–1:2) | Too short |
|-----------|-----------------|----------------|--------------------|-----------------| ----------|
| **Perceived strength** | Very low | Lower | | Higher | Very high |
| **Taste / mouthfeel** | Higher clarity | | | More body | |
| **Extraction** | Too high | Higher | | Lower | Too low |

### Taste vs Extraction

| Over-extracted | Sweet spot | Under-extracted |
|---------------|------------|-----------------|
| Bitter, thin, watery, astringent, burnt, drying, lacking | Sweet, ripe, balanced and rich taste | Sour, mouth-puckering, distracting, salty |

### Ratio and Flow Rate Relationship

Higher ratios (1:3+) generally pair with faster flow for clarity. Shorter ratios (1:1.5–1:2) pair with slower flow for body. When adjusting ratio, consider adjusting flow rate in the same direction to maintain balance.

---

## 8. Flavor & Texture Targeting

How to increase the likelihood of achieving certain flavors and textures.

(↑ = increase/darker/finer/longer/higher, ↓ = decrease/lighter/coarser/shorter/lower, ? = uncertain)

| Want more: | Roast level | Temp | Grind size | Infusion | Peak pressure | Flow rate | Ratio (1:X) |
|------------|-------------|------|------------|----------|---------------|-----------|-----------|
| **Acidity** | ↓ lighter | ↓ | ↑ finer | ↑ longer | ↓ | ↑ higher | ?? |
| **Sweetness** | ↑? | ↑? | ↓? finer | | ↑ | ↑↓? | Match endflow to ratio |
| **Body, syrupy texture** | ↑ darker | ↓ | ↓ coarser | ↓ shorter | ↑ | ↓ lower | ↓ shorter |
| **Wine-like texture** | ↓ lighter | ↑ | ↑ finer | ↑ longer | ↓ | ↑ higher | ↑ longer |
| **Clarity of flavour** | ↓ lighter | ↑ | ↑ finer | ↑ longer | ↓ | ↑ higher | ↑ longer |

Notes:
- Intensity of flavour → increase EY%? Increase dose?
- Length of aftertaste → same approach?

---

## 9. Flavor Correction Guide

How to correct some perceived unpleasant tastes. All corrections should be made in small iterations — try not to change more than one variable at a time. Sometimes going too far in one direction can have the opposite effect (like grinding too fine).

| Problem | Roast level | Temp | Grind size | Infusion | Peak pressure | Flow rate | Ratio (1:X) |
|---------|-------------|------|------------|----------|---------------|-----------|-----------|
| **Sour & salty** | ↑ darker | ↑ | ↓ finer | | ↑ | ↑? | ? |
| **Harshness, bitterness & astringency** | ↓ lighter | ↓ | ↑ coarser | ↑ longer | ↓ | ↓? | ? |
| **Burnt-wood / ashen taste** | ↓ lighter | ↓ | ↑ coarser | ↑ longer | ↓ | ↑ | ↑ longer |
| **Thin watery taste** | ? | ↑? | ↓ finer | ↓ shorter | | ↓ lower | ↓ shorter |
| **Muddy cup lacking clarity** | ↓ lighter | ↑ | ↑ coarser | ↑ longer | ↓ | ↑ higher | ↑ longer |

---

## 10. Taste vs TDS and EY%

Relationships between Total Dissolved Solids (concentration) and Extraction Yield (how much was extracted):

| Taste attribute | TDS | EY% | Explanation |
|----------------|-----|-----|-------------|
| **Bitterness** | ↑ increases | ↑ increases | Both higher TDS and higher EY% increase bitterness |
| **Burnt-wood / ash** | ↑ increases | ↑ increases | Intensifies when both EY% and TDS increase |
| **Sourness** | ↑ increases | ↓ decreases | The stronger you make the coffee OR the less you extract, the more sour it gets |
| **Dark chocolate** | ↓ decreases | ↑ increases | High extraction percentage but lower TDS maximizes dark chocolate attributes |
| **Sweetness** | Complex | Balanced–high | Aim for balanced or higher EY% that still tastes good. With some grinders, both higher TDS and higher yield can increase sweetness. Higher yield increases clarity, making sweetness more perceivable. Avoid higher temperatures to reduce bitter compound extraction. |
