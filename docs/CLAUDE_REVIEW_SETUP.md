# Claude pull-request reviewer

`.github/workflows/claude-review.yml` uses the official Anthropic action to post
up to five inline findings on relevant, non-draft pull requests opened, updated,
reopened, or marked ready for review. It focuses on Qt/C++ lifetimes, threading,
QML boundaries, BLE/machine-state regressions, database handling, and platform bugs.
It reads source and existing tests; it never builds or runs the application.

## One-time setup

Both workflows use **Workload Identity Federation (WIF)** for the Claude API.
GitHub issues a short-lived OIDC identity token; the official action exchanges it
for a short-lived Anthropic token and handles renewal. No stored Anthropic API key
or subscription OAuth token is needed. Non-secret identifiers still live in
repository Actions variables. Tokens exist temporarily at runtime, and neither
Claude's read tools nor public artifacts should expose the action's token cache.

1. An Anthropic organization admin/owner opens
   [Claude Console](https://platform.claude.com/) → **Settings → Workload identity →
   Connect workload → GitHub Actions**. Select the organization/workspace holding
   your API credits, set a workspace spending limit, and confirm Sonnet model access.
2. Register the issuer `https://token.actions.githubusercontent.com` using JWKS
   **discovery**. Create a service account named, for example, `decenza-ci`, and add
   it to the selected workspace. Create two federation rules using the matches
   below. Scope both rules to that one workspace with `workspace:developer` and a
   600-second token lifetime; keep single-use assertion replay protection enabled.
3. A repository admin opens
   [Decenza's Actions variables](https://github.com/Kulitorum/Decenza/settings/variables/actions)
   and creates the five variables in the table below. These IDs are not secrets.
4. Install the Claude App if requested fixes are needed, as described below.
   Review and merge the federation workflow PR when ready. Do not make the Claude job a
   required merge check: path filters and skipped runs make it advisory.
5. Open a small, non-draft PR from a branch inside `Kulitorum/Decenza`, authored
   and triggered by a human with repository write access. Include a relevant
   source change. In **Actions → Claude PR review**, check that the Claude step
   completes, and inspect any inline findings. The job should be skipped on
   drafts, fork PRs, and other bots. A run with no findings posts no comment.

| Repository Actions variable | Value from Claude Console |
| --- | --- |
| `ANTHROPIC_ORGANIZATION_ID` | Organization UUID |
| `ANTHROPIC_SERVICE_ACCOUNT_ID` | `svac_...` for the service account |
| `ANTHROPIC_WORKSPACE_ID` | `wrkspc_...` for the selected workspace |
| `ANTHROPIC_REVIEW_FEDERATION_RULE_ID` | `fdrl_...` for the review rule |
| `ANTHROPIC_FIX_FEDERATION_RULE_ID` | `fdrl_...` for the requested-fix rule |

### Federation rule matches

Both rules must require audience `https://api.anthropic.com`. Set the following
exact claim matches on **both** rules:

```json
{
  "repository": "Kulitorum/Decenza",
  "repository_id": "1121207637",
  "repository_owner_id": "175644",
  "runner_environment": "github-hosted"
}
```

For the **review rule**, also match `event_name` exactly to `pull_request` and add
this CEL condition, which permits only this review workflow on PR merge refs:

```text
claims.workflow_ref.matches("^Kulitorum/Decenza/\\.github/workflows/claude-review\\.yml@refs/pull/[0-9]+/merge$")
```

For the **fix rule**, also add these exact claim matches:

```json
{
  "event_name": "issue_comment",
  "ref": "refs/heads/main",
  "workflow_ref": "Kulitorum/Decenza/.github/workflows/claude-fix.yml@refs/heads/main"
}
```

All populated matchers must pass. Bind the two rules to the same service account
and selected workspace. These matches use immutable repository/owner IDs rather
than relying only on a name or a broad subject prefix. They do not require a
particular `sub` format, which can vary with GitHub's immutable/custom subjects.
Workflow guards and actor permission checks still exclude untrusted fork and
non-maintainer requests. Changing the default branch or workflow filename needs a
matching rule update. Review rules authorize trusted same-repository PR workflows;
repository write access remains a trust boundary for changing those workflows.

The Console wizard can test the connection; a draft PR is skipped and will not
produce an exchange. Use a non-draft eligible PR after the configuration is ready,
and check Console authentication history plus the action's sanitized status.
Never print or upload the raw identity/access token when troubleshooting.

If migrating from the API-key version, configure the rules/variables before
merging. The workflows no longer pass `ANTHROPIC_API_KEY`; they do not silently
fall back to it. After a successful federated run, remove any obsolete dedicated
repository secret and revoke that dedicated key in Console if no other workload
uses it. Federation changes authentication, not the API billing account: usage is
attributed to the chosen service account's workspace under its normal limits.

## GitHub permissions and the Claude App

The automatic review workflow requires **no Claude GitHub App installation, App
private key, personal access token, or stored Anthropic credential**. It explicitly
passes GitHub's short-lived `GITHUB_TOKEN` for repository operations. Its job grants
`contents: read`, `pull-requests: write`, and `id-token: write`; all other
permissions are disabled. OIDC permission allows Anthropic authentication and
does not grant code-write permission.
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

Missing configuration: add the named repository Actions variables. Authentication
failure: check Console authentication history, issuer/audience, claim matches,
service-account workspace membership, credits, and model access. Do not weaken
the rule to a broad repository subject prefix to hide a mismatch. An API-key or
OAuth credential injected elsewhere takes precedence over federation; remove
that injection rather than storing an empty credential value.
`Resource not accessible by integration`: check repository/organization Actions
policies allow this action and PR review comments. Keep contents read-only.
Fork/draft/bot PR: skipped by design. Review a same-repository human PR to test.

To stop spending, disable **Claude PR review** from its Actions workflow menu.
Disable **Claude requested fix** separately to stop accepting fix requests.
Archiving the two federation rules in Console prevents future token exchanges;
already-issued tokens remain subject to their expiry. Do not delete a shared
issuer/service account used by another workload.

Sources: [action setup](https://github.com/anthropics/claude-code-action/blob/main/docs/setup.md),
[action security](https://github.com/anthropics/claude-code-action/blob/main/docs/security.md),
[CLI limits/tools](https://code.claude.com/docs/en/cli-reference),
[models](https://platform.claude.com/docs/en/models/overview),
[Anthropic federation](https://platform.claude.com/docs/en/manage-claude/workload-identity-federation),
[rule matching](https://platform.claude.com/docs/en/manage-claude/wif-reference#rule-matching-semantics),
[GitHub OIDC claims](https://docs.github.com/en/actions/reference/security/oidc).
