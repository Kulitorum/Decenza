#pragma once

#include <QJsonObject>
#include <QString>

// Request-shape rules every caller of a cloud AI provider must apply: the
// advisor (aiprovider.cpp) and the bulk translator (translationmanager.cpp),
// which once hand-rolled its own bodies and missed them.
//
// Header-only, depending on nothing but QJsonObject, on purpose:
// decenza_testlib compiles translationmanager.cpp but not the AI stack, and
// aiprovider.h would drag the provider classes into every test target that
// links it (count: `grep -c "^add_decenza_test(" tests/CMakeLists.txt`).

namespace AIRequestShape {

// The output cap every cloud request shares (AIProvider::MAX_OUTPUT_TOKENS is this).
constexpr int kMaxOutputTokens = 4096;

// The thinking/reasoning setting, sent explicitly on every request and per
// model, because the accepted values differ and a wrong one 400s every request.
// Lowest is right for both callers: hidden thinking is billed at the output
// rate, and on OpenAI reasoning cost the advisor its nextShot block (counts in
// tools/ai_model_eval/README.md, 2026-07-30). Omitting the field is not an
// option on Anthropic: newer models then think adaptively, which can use the
// whole max_tokens budget and return no text (#1691).
//
// The direct-provider values were verified live on 2026-10-05, OpenRouter's on
// 2026-10-06 (tools/ai_model_eval). A model added to a catalog must be probed
// and added here and to tst_aiproviders' verified table.

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

// The generationConfig both Gemini callers send; maxOutputTokens bounds thinking too.
inline QJsonObject geminiGenerationConfig(const QString& model)
{
    return QJsonObject{{QStringLiteral("thinkingConfig"), geminiThinkingConfig(model)},
                       {QStringLiteral("maxOutputTokens"), kMaxOutputTokens}};
}

// OpenRouter: openai/gpt-6-luna takes "none" (1.1-1.4K reasoning tokens per
// reply without it). Sol, Sonnet 5.5, 3.8 Flash and GLM-5.3 Flash make
// reasoning mandatory and 400 on "none"; the first three reasoned 0-62 tokens
// at "low". GLM and Gemma 4 were judged at these settings. Only catalog models
// reach here.
inline const QString kOpenRouterDefaultModel = QStringLiteral("openai/gpt-6-luna");
inline const QString kOpenRouterGemmaModel = QStringLiteral("google/gemma-4-31b-it");
inline void setOpenRouterReasoning(QJsonObject& requestBody, const QString& model)
{
    if (model == kOpenRouterGemmaModel)
        requestBody["reasoning"] = QJsonObject{{QStringLiteral("enabled"), true}};
    else
        requestBody["reasoning"] = QJsonObject{{QStringLiteral("effort"),
            model == kOpenRouterDefaultModel ? QStringLiteral("none") : QStringLiteral("low")}};
}
}  // namespace AIRequestShape
