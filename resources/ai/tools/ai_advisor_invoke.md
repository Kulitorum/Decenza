# ai_advisor_invoke

Runs the configured AI advisor over a shot's dial-in context, building the same system and user
prompt the in-app advisor would build and sending it to the provider currently selected in
settings (OpenAI / Anthropic / Gemini / OpenRouter / Ollama). No other provider is ever
substituted.

## Response

- `response` — the advisor's text.
- `systemPromptUsed` / `userPromptUsed` — always echoed, so a caller can see exactly what was
  sent. This is what makes the tool usable for prompt A/B testing and end-to-end validation.
- `structuredNext` — present only when the reply makes a concrete recommendation (grind, dose,
  profile change). It is the trailing fenced ```json `nextShot` block defined in the system
  prompt, parsed out and surfaced at the top level. **Omitted entirely** — no null placeholder —
  when the reply is a clarifying question or otherwise carries no recommendation.

## dryRun

`dryRun: true` skips the network call and returns the assembled prompts only: no tokens spent.
It still spawns a worker thread and reads the shot row from SQLite.

## Side effects and failure

When not a dry run, the response also reaches the in-app conversation overlay: it updates
`lastRecommendation` and fires `recommendationReceived`. The tool returns an error if the advisor
is already busy with another request.

Optional overrides let a caller substitute custom system/user prompts to test alternate prompt
shapes against the same provider configuration.
