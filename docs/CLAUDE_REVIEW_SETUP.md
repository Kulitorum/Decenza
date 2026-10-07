# Claude pull-request reviewer

`.github/workflows/claude-review.yml` uses the official Anthropic action to post
up to five inline findings on relevant, non-draft pull requests opened, updated,
reopened, or marked ready for review. It focuses on Qt/C++ lifetimes, threading,
QML boundaries, BLE/machine-state regressions, database handling, and platform bugs.
It reads source and existing tests; it never builds or runs the application.

## One-time setup

1. In [Claude Console](https://platform.claude.com/), select the organization and
   workspace that hold your Anthropic API credits. Create a dedicated API key for
   Decenza reviews, and set an appropriate workspace spending limit. Check that
   this workspace can use `claude-sonnet-5-5` before enabling reviews.
2. A repository admin must open
   [Decenza's Actions secrets](https://github.com/Kulitorum/Decenza/settings/secrets/actions),
   choose **New repository secret**, and name it `ANTHROPIC_API_KEY`. Paste the key
   directly into GitHub. Never put it in a PR, source file, chat, or screenshot.
   This uses API billing/credits, rather than a Claude subscription OAuth token.
3. Review and merge the workflow PR when ready. Do not make the Claude job a
   required merge check: path filters and skipped runs make it advisory.
4. Open a small, non-draft PR from a branch inside `Kulitorum/Decenza`, authored
   and triggered by a human with repository write access. Include a relevant
   source change. In **Actions → Claude PR review**, check that the Claude step
   completes, and inspect any inline findings. The job should be skipped on
   drafts, fork PRs, and bot events. A run with no findings posts no comment.

## GitHub permissions and the Claude App

This workflow requires **no Claude GitHub App installation, App private key,
personal access token, OAuth token, or OIDC permission**. It explicitly passes
GitHub's short-lived `GITHUB_TOKEN` to the official action. Its job grants only
`contents: read` and `pull-requests: write`; all other permissions are disabled.
Comments appear as `github-actions[bot]`. The token cannot push code or merge PRs.

The official [Claude App](https://github.com/apps/claude) is useful for broader
`@claude` coding workflows. Its default action token has contents, issues, and
pull-request write access. Declaring `contents: read` in a job does not by itself
restrict that separate App token. Keep the explicit `github_token` input here.
If you install the App for other workflows, select **Only select repositories →
Decenza** and inspect the installation's requested permissions. The official
App's permission set cannot be individually reduced by an installer; a custom App
is needed for a different permission set. Neither is needed for this reviewer.

## Limits and security

- Only same-repository human PRs run. The action also checks that the triggering
  actor has repository write access; no `allowed_bots` or `allowed_non_write_users`
  bypass is configured. Fork reviews require a separate design, not switching
  this job to `pull_request_target` or granting more permissions.
- Base and head are checked out by immutable SHA, without persisted credentials.
  The head lives in a subdirectory, and Claude ignores project/local executable
  settings and discovers only the action's explicit inline-comment MCP server.
  Claude can read/search source and post inline comments; shell, editing, skill,
  and delegation tools are unavailable. No PR-controlled scripts execute.
- Each run stops after 20 turns or 15 minutes, with a $3 client-side API cost
  limit. The estimate can differ from the bill, and cancellation does not undo
  charges already incurred. Use Console workspace limits for total spend control.
  New pushes cancel older runs for the same PR. Reruns can repeat comments.
- Full output and report display are disabled. Do not enable Actions debug
  logging: the action can enable full output in debug mode. Do not upload the
  action's execution files as public artifacts.
- A successful workflow means the automation completed, not that the code is
  bug-free. Check logs for turn/budget limits or permission failures; keep the
  existing text-invariant checks and local/nightly test process.

## Troubleshooting and disabling

Missing key or authentication failure: have an admin add/rotate the repository
secret, and confirm the key's organization, credits, and model access in Console.
`Resource not accessible by integration`: check repository/organization Actions
policies allow this action and PR review comments. Keep contents read-only.
Fork/draft/bot PR: skipped by design. Review a same-repository human PR to test.

To stop spending, disable **Claude PR review** from its Actions workflow menu.
Deleting the `ANTHROPIC_API_KEY` repository secret also prevents future API runs;
rotate/revoke the dedicated key in Console if it is no longer needed.

Sources: [action setup](https://github.com/anthropics/claude-code-action/blob/main/docs/setup.md),
[action security](https://github.com/anthropics/claude-code-action/blob/main/docs/security.md),
[CLI limits/tools](https://code.claude.com/docs/en/cli-reference),
[models](https://platform.claude.com/docs/en/models/overview).
