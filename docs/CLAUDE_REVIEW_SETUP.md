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
   drafts, fork PRs, and other bots. A run with no findings posts no comment.

## GitHub permissions and the Claude App

The automatic review workflow requires **no Claude GitHub App installation, App private key,
personal access token, OAuth token, or OIDC permission**. It explicitly passes
GitHub's short-lived `GITHUB_TOKEN` to the official action. Its job grants only
`contents: read` and `pull-requests: write`; all other permissions are disabled.
Comments appear as `github-actions[bot]`. The token cannot push code or merge PRs.

For requested fixes, an admin must install the official
[Claude App](https://github.com/apps/claude). Select **Only select repositories →
Decenza** and inspect the requested permissions. The official App requests
contents, pull-request, issue, discussion, and workflow write access, plus
Actions/checks read access; the installation screen is authoritative. Its
permission set cannot be individually reduced by the installer. The fix job
requests a repository-scoped token with contents/pull-request write and issue
read access via `additional_permissions`, using `id-token: write` for exchange.
The App token is revoked when the action finishes. No App private key is needed.

The review job still explicitly passes `GITHUB_TOKEN`, even after App installation.
Its read-only contents restriction therefore continues to apply. Job permissions
alone do not restrict a separate App token.

## Maintainer-requested fixes

After the workflows and trusted publisher are merged, a human with repository
write access can post a new **PR conversation comment** whose first line is:

```text
@claude fix
```

This creates a separate draft PR targeting the original PR's feature branch,
so it shows only the proposed fixes. To update the existing PR branch instead:

```text
@claude fix same-pr
```

Add specific instructions below that first line to select findings to fix.
Without those instructions, Claude checks the inline feedback and reviews for
concrete unresolved bugs introduced by the PR. These commands work on open,
same-repository feature-branch PRs; they do not run from inline review replies,
forks, bots, or edited comments. Requests are checked against actual write access.

Claude edits an isolated checkout and can invoke only the trusted publisher as a
shell command. The publisher checks the PR's captured head SHA, accepts at most
20 source files, rejects workflow/agent configuration and symlinks, and chooses
the destination from the original command. It never force-pushes, approves, or
merges. Same-PR fixes post a validation-status comment and trigger another review.
Separate drafts are reviewed when a maintainer marks them ready.

Fixes have a $5 client-side cost limit, 30 turns, and 20 minutes. Qt builds and
tests **do not run** on this hosted fix runner. Review the diff and validate fixes
through the existing Qt Creator test process before merging; publication does not
claim the fix is validated. If publication stops partway through, inspect the
source/fix branch before retrying. Disable **Claude requested fix** in Actions
to stop accepting fix requests.

## Limits and security

- Reviews accept human PRs and the official `claude[bot]` so requested fixes can
  be reviewed. Other bots are skipped. Human triggers must have write access;
  no `allowed_non_write_users` bypass is configured. Fork reviews require a separate design, not switching
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
