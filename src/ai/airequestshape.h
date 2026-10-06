#pragma once

#include <QJsonObject>
#include <QString>

// Request-shape rules that EVERY caller of a cloud AI provider must apply,
// in one place.
//
// This header exists because there are two independent call sites — the
// advisor (`src/ai/aiprovider.cpp`) and the bulk translator
// (`src/core/translationmanager.cpp`) — and the translator was hand-rolling
// its own request bodies, so it silently missed both rules below. A user who
// picked Sonnet 5 for the advisor got it for translation too, with thinking
// left at the model default; see disableAnthropicThinking() for why that
// returns an empty reply.
//
// It is header-only and depends on nothing but QJsonObject ON PURPOSE.
// `decenza_testlib` compiles translationmanager.cpp but not the AI stack, so
// pulling in aiprovider.h would drag the provider classes into every test
// target that links it — 106 of them at last count — to read two lines of
// JSON. Keep it that way: no provider types, no networking, no settings.
// (Stated as "forty-odd" when written, copying a "~30" figure from
// tests/CMakeLists.txt that was already years stale. Count it, don't recall
// it: `grep -c "^add_decenza_test(" tests/CMakeLists.txt` — anchored, since
// the comment carrying this recipe over there contains the search string and
// an unanchored grep counts itself, reporting 107.)

namespace AIRequestShape {

// The output cap every cloud request shares. Mirrored by
// AIProvider::MAX_OUTPUT_TOKENS, which cannot be used directly here for the
// same reason this header exists — aiprovider.h is not reachable from the
// translator's translation unit. Kept as one definition rather than a literal
// repeated per call site.
constexpr int kMaxOutputTokens = 4096;

// Turn model thinking/reasoning OFF, explicitly, on every request — per model,
// because the accepted "off" differs by model and a wrong one 400s every request.
//
// Off is right for both callers: dial-in advice and bulk translation need
// little chain-of-thought, hidden thinking is billed at the output rate, and
// on OpenAI reasoning measurably costs the advisor its trailing `nextShot`
// block (counts in docs/CLAUDE_MD/AI_ADVISOR.md). Omitting the field is not an
// option on Anthropic: newer models then run ADAPTIVE thinking, which can use
// the whole max_tokens budget and return no text block (#1691).
//
// Each value below was verified live on 2026-10-05 with
// tools/ai_model_eval/probe_request_shape.py — accepted, and for Anthropic still
// returning a text block, for Gemini reporting no thinking tokens. A model added
// to a catalog must be probed and added here; the fallbacks are the older
// generations' forms, which a new model may reject.

// claude-sonnet-5-5 rejects {"type":"disabled"} and asks for "between_tools"
// (no thinking before the reply); older models take "disabled".
inline void disableAnthropicThinking(QJsonObject& requestBody, const QString& model)
{
    QJsonObject thinking;
    thinking["type"] = model == QLatin1String("claude-sonnet-5-5") ? QStringLiteral("between_tools")
                                                                     : QStringLiteral("disabled");
    requestBody["thinking"] = thinking;
}

// gpt-6.1-sol accepts low/medium/high/xhigh only; the rest take "none".
inline void disableOpenAIReasoning(QJsonObject& requestBody, const QString& model)
{
    requestBody["reasoning_effort"] = model == QLatin1String("gpt-6.1-sol") ? QStringLiteral("low")
                                                                             : QStringLiteral("none");
}

// A non-default temperature, where the model takes one: gpt-6.1-sol at its
// lowest effort accepts only the default (1) and 400s on anything else.
inline void setOpenAITemperature(QJsonObject& requestBody, const QString& model, double temperature)
{
    if (model != QLatin1String("gpt-6.1-sol"))
        requestBody["temperature"] = temperature;
}

// generationConfig.thinkingConfig. The 2.x family takes an integer budget (0 =
// off); 3.x takes thinkingLevel, whose legal values vary by model —
// gemini-3.8-flash rejects "minimal" and reports no thinking at "low".
inline QJsonObject geminiThinkingConfig(const QString& model)
{
    QJsonObject config;
    if (model.startsWith(QLatin1String("gemini-2")))
        config["thinkingBudget"] = 0;
    else
        config["thinkingLevel"] = QStringLiteral("low");
    return config;
}

// OpenRouter reasoning per catalog model, live 2026-10-06 on real advisor
// prompts. openai/gpt-6-luna takes "none" (1.1-1.4K reasoning tokens per reply
// without it). Sol, Sonnet 5.5, 3.8 Flash and GLM-5.3 Flash make reasoning
// mandatory and 400 on "none"; at "low" the first three reasoned 0-62 tokens.
// GLM and Gemma 4 gave wrong grind advice with reasoning off or at their
// default and right advice at these settings. Only catalog models reach here.
inline const QString kOpenRouterDefaultModel = QStringLiteral("openai/gpt-6-luna");
inline const QString kOpenRouterGemmaModel = QStringLiteral("google/gemma-4-31b-it");
inline void disableOpenRouterReasoning(QJsonObject& requestBody, const QString& model)
{
    if (model == kOpenRouterGemmaModel)
        requestBody["reasoning"] = QJsonObject{{QStringLiteral("enabled"), true}};
    else
        requestBody["reasoning"] = QJsonObject{{QStringLiteral("effort"),
            model == kOpenRouterDefaultModel ? QStringLiteral("none") : QStringLiteral("low")}};
}
}  // namespace AIRequestShape
