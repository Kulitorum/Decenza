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
// pulling in aiprovider.h would drag the provider classes into forty-odd test
// targets to read two lines of JSON. Keep it that way: no provider types, no
// networking, no settings.

namespace AIRequestShape {

// The output cap every cloud request shares. Mirrored by
// AIProvider::MAX_OUTPUT_TOKENS, which cannot be used directly here for the
// same reason this header exists — aiprovider.h is not reachable from the
// translator's translation unit. Kept as one definition rather than a literal
// repeated per call site.
constexpr int kMaxOutputTokens = 4096;

// Turn Anthropic extended thinking OFF, explicitly, on every request.
//
// This has to be explicit because the default is NOT stable across models.
// Per Anthropic's thinking documentation (checked 2026-07-29): omitting the
// `thinking` field runs NO thinking on claude-sonnet-4-6, but runs ADAPTIVE
// thinking on claude-sonnet-5. Since max_tokens caps thinking + response text
// together, omitting it on Sonnet 5 lets thinking consume the entire budget
// and the reply carries no text block at all.
//
// That is the mechanism behind #1691, whose user-visible symptom was
// "Anthropic returned empty response content" on every request. Note the issue
// itself reports only the symptom — it names no model and carries no wire
// capture. The mechanism is inferred from the documented defaults.
//
// Off (not merely bounded) is the right setting for both callers: dial-in
// advice and bulk translation both need little chain-of-thought, and hidden
// thinking tokens are billed at the output rate.
//
// VERIFIED live 2026-07-30 for both current catalog entries: claude-sonnet-4-6
// and claude-sonnet-5 each accept type "disabled" AND return a `text` block
// (a thinking-only reply with no text block is the #1691 symptom, so the block
// types are the thing to check, not just the HTTP status).
//
// INVARIANT, still live for anything ADDED later: newer Anthropic models may
// reject the disabled form outright (thinking always on, omit the field
// instead) or accept it only at or below a given effort level. Such a model
// would turn EVERY Anthropic request into a 400 — a worse #1691 than #1691.
// Re-run tools/ai_model_eval/ probes when the catalog changes.
inline void disableAnthropicThinking(QJsonObject& requestBody)
{
    QJsonObject thinking;
    thinking["type"] = QStringLiteral("disabled");
    requestBody["thinking"] = thinking;
}

// Turn OpenAI reasoning off on a chat/completions request.
//
// The GPT-5 family are reasoning models. Reasoning measurably costs the
// advisor the trailing `nextShot` JSON block, and for translation it is spend
// with no upside. NOT because reasoning tokens overrun the output cap — that
// was the original rationale here and a replay refuted it; the models finish
// cleanly and simply omit the block while reasoning. Counts, method and the
// refutation live in docs/CLAUDE_MD/AI_ADVISOR.md and tools/ai_model_eval/,
// so they don't rot in a comment.
//
// The 5.4 generation REPLACED the value "minimal" with "none" (live-caught
// 400: supported = none/low/medium/high); 5.6 accepts "none" as well.
//
// INVARIANT: assumes every model either caller can select is a reasoning model
// that accepts "none" — guard/branch here if the catalog gains one that isn't.
inline void disableOpenAIReasoning(QJsonObject& requestBody)
{
    requestBody["reasoning_effort"] = QStringLiteral("none");
}

}  // namespace AIRequestShape
