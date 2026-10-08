# Claude pull-request reviewer

`.github/workflows/claude-review.yml` uses the official Anthropic action to post
up to five inline findings. Automatic reviews run on relevant, non-draft PRs
opened by **skialpine** (GitHub account ID `1629894`), on open, new commits,
reopen, or ready-for-review. Other PRs require skialpine's explicit request. It focuses on Qt/C++ lifetimes, threading,
QML boundaries, BLE/machine-state regressions, database handling, and platform bugs.
It reads source and existing tests; it never builds or runs the application.

## One-time setup

Both workflows use **Workload Identity Federation (WIF)** for the Claude API.
GitHub issues a short-lived OIDC identity token; the official action exchanges it
for a short-lived Anthropic token and handles renewal. No stored Anthropic API key
or subscription OAuth token is needed. The configured IDs below are not secrets
and are included directly in both workflows; no Actions variables are required.

| Federation setting | Configured value |
| --- | --- |
| Federation rule | `fdrl_01845VJkqDXmxfEeoz22VNyg` |
| Organization | `e3971c6d-a39b-46fc-a5e9-d152d5f5d476` |
| Service account | `svac_01Rjzffi5Zn9hAG3NSPrboZE` |
| Workspace | `wrkspc_01CfPYtm6bgzXkKPucdyGATe` |
| OIDC audience | `https://api.anthropic.com` |

1. The Anthropic Console rule is configured for repository **Kulitorum/Decenza**,
   with the branch left unrestricted. Confirm the service account belongs to the
   selected workspace, that this is the organization/workspace holding your API
   credits, and that it can use `claude-sonnet-5-5`. Set a workspace spending limit.
2. Install the Claude App if requested fixes are needed, as described below.
   Review and merge the federation workflow PR when ready. Keep the Claude review
   job advisory: its path filters and skipped runs make it unsuitable as a required
   merge check.
3. Open a small, non-draft PR from a branch inside `Kulitorum/Decenza`, authored
   by skialpine. In **Actions → Claude PR review**,
   check completion and inspect any inline findings. Drafts, forks, and other bots
   are skipped. Completed reviews update a Claude status comment on the PR even
   with zero findings. Failed reviews report incomplete status, never zero findings.

### Verified token exchange

[The federation test run](https://github.com/Kulitorum/Decenza/actions/runs/37706355051)
successfully exchanged a GitHub identity token for an Anthropic bearer token with
599 seconds of remaining lifetime. It made no model call and exported/logged
neither token. This confirms authentication for the setup-branch push; it does
not verify model access, credits, or the complete review/fix execution.

`.github/workflows/anthropic-wif-test.yml` runs on changes to itself on the setup
branch and can be dispatched manually after merge. It uses the same four IDs and
audience as the production workflows. No checkout, API key, or Claude App is
needed for this exchange-only test. Its manual `test_inference` option makes one
tiny paid Messages request (16 output tokens maximum) to the same model and
workspace. Only skialpine can run the diagnostic. It reports HTTP status, an
allowlisted error type, and a safe category such as `workspace_spending_limit`;
tokens, model output, and raw error bodies stay private. This option defaults off.

### Trust scope and migration

The rule's unrestricted branch setting permits authentication from all Decenza
branches. Automatic reviews and fix requests reject forks; explicit read-only
reviews accept forks only on skialpine's request. Anyone able to write Decenza workflows is a trust
boundary, because they can request an identity token from another workflow.

For stronger isolation, an Anthropic admin can additionally require immutable
`repository_id: 1121207637` and `repository_owner_id: 175644`, plus the exact
workflow/event pair. Automatic review tokens use `pull_request` with
`Kulitorum/Decenza/.github/workflows/claude-review.yml@refs/pull/<number>/merge`;
Requested review tokens use `issue_comment` or `workflow_dispatch` with
`Kulitorum/Decenza/.github/workflows/claude-review.yml@refs/heads/main`;
fix tokens use `issue_comment` with
`Kulitorum/Decenza/.github/workflows/claude-fix.yml@refs/heads/main`.
Separate rules can express those restrictions. If keeping this diagnostic test,
its workflow/event must also be permitted. Such Console rule changes have not
been applied by this PR.

The workflows no longer pass `ANTHROPIC_API_KEY` and do not fall back to it.
After a successful federated production run, remove any obsolete dedicated
repository secret and revoke that dedicated key in Console if no other workload
uses it. Federation changes authentication, not the API billing account: usage is
attributed to the chosen service account's workspace under its normal limits.
Tokens exist only at runtime; Claude's read tools deny access to the action's
temporary identity-token and credential-cache directory.

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

## Requesting a review

Automatic reviews run only on skialpine's same-repository, non-draft PRs touching
the configured source paths. Commits pushed by the official Claude App can
re-trigger a review on those PRs. PRs opened by anyone else, including Claude,
do not receive automatic reviews.

For any other open PR, skialpine can post this standalone **PR conversation
comment**:

```text
@claude review
```

This requests one review of the current revision. Only skialpine's account ID
can trigger it; other people's requests are skipped. It also works on draft PRs
and public fork PRs, and bypasses the automatic path filter. Another push to
someone else's PR requires another request. Edited comments, ordinary issue
comments, and inline review replies do not trigger the command. Alternatively,
skialpine can choose **Actions → Claude PR review → Run workflow**, enter the PR
number, and run a review. This explicit dispatch uses the same read-only review
and publication checks.

The requested review is read-only. Its workflow runs from the default branch,
checks out the trusted base at the workspace root, and reads the captured head
in an isolated subdirectory without executing its code. The diff is indexed into bounded per-file chunks, then assigned to sequential
batches of at most eight files and eight chunks (at most 65 KB of patch input).
Large files can span batches. Every assigned patch is supplied directly in the
initial review prompt, so reading a subset of patch files cannot silently omit
the remaining patch input. Each batch can read surrounding source as needed. Every batch must complete before publication. The publisher independently checks
that chunk assignments are lossless and unique and that each successful result
matches the hash/count/size of the patch text supplied to its action. This verifies
input delivery, not the model's depth of analysis: assessment completion is still
reported by Claude. Claude returns structured findings; the workflow checks every batch result,
deduplicates findings, selects up to five by severity, and validates their paths,
diff lines, and captured revision before posting a COMMENT review. It cannot approve or change code. A source
update during context/diff fetching or review prevents stale publication. Unrelated comments do not cancel a
review in progress. The command becomes available after this workflow is merged
to the default branch.

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
merges. Same-PR fixes on skialpine's eligible PRs trigger another review.
Fixes on other PRs, including separate Claude-authored drafts, require skialpine
to post `@claude review` when a review is wanted.

Fixes have a $5 client-side cost limit, 30 turns, and 20 minutes. Qt builds and
tests **do not run** on this hosted fix runner. Review the diff and validate fixes
through the existing Qt Creator test process before merging; publication does not
claim the fix is validated. If publication stops partway through, inspect the
source/fix branch before retrying. Disable **Claude requested fix** in Actions
to stop accepting fix requests.

## Limits and security

- Automatic reviews accept only PRs opened by skialpine. Explicit review
  requests accept only skialpine's conversation comment. Automatic triggers from
  bots other than the official `claude[bot]` are skipped. Human triggers must have write access;
  no `allowed_non_write_users` bypass is configured. Forks are reviewed only on
  skialpine's explicit request, using the trusted `issue_comment` workflow and
  read-only source access.
- Base and head are checked out by immutable SHA, without persisted credentials.
  The head lives in a subdirectory, and Claude ignores project/local executable
  settings. Claude can only read/search source and return findings; shell,
  editing, skill, delegation, and MCP tools are unavailable. A fixed workflow
  step validates and publishes the comments. No PR-controlled scripts execute.
- Each batch has up to 40 turns and five minutes. A total $3 client-side API
  estimate cap is divided among all batches according to patch size and context
  overhead. Small PRs use one batch; large PRs take longer because batches run
  sequentially. Estimates can differ from the bill. Cancellation does not undo
  charges already incurred. Use Console workspace limits for total spend control.
  New pushes cancel older runs for the same PR. Reruns update the status comment;
  inline findings can repeat. Artifact and script actions use Node 24.
- Only public PR context and sanitized batch findings are shared as one-day
  workflow artifacts. Raw execution traces and credential caches stay local to
  each runner. Full output and report display are disabled. Do not enable Actions debug
  logging: the action can enable full output in debug mode. Do not upload the
  action's execution files as public artifacts.
- Completion means every batch reported its static source assessment completed, not that runtime
  behavior was exhaustively verified or that the code is bug-free. Qt build/tests
  remain separate. Incomplete results report a reason (missing input, unreadable
  source, analysis limit, or other) and publish no partial findings. Publication
  failures are separate from analysis failures. Keep the existing text-invariant
  checks and local/nightly test process.

## Troubleshooting and disabling

A near-instant failure with no tools and no structured result can be a provider
rejection before any review. The collector reports a safe error category without
printing the private response. `insufficient_credits`, `spending_limit`, or
`billing_or_quota`: check the selected organization's API credit balance and the
federated workspace's spending/rate limits in Claude Console. A successful token
exchange does not test credits. These failures are not automatically retried;
request a new review after resolving the account limit. The workflow never raises
Console limits or switches to a different paid account.

Authentication failure: check Console authentication history, issuer/audience,
repository/branch restrictions, service-account workspace membership, credits,
and model access. An API-key or OAuth credential injected elsewhere takes
precedence over federation; remove that injection rather than storing an empty
credential value. If reconfiguring the service account/workspace/rule, update
the non-secret IDs in both production workflows and the diagnostic workflow.
`Resource not accessible by integration`: check repository/organization Actions
policies allow this action and PR review comments. Keep contents read-only.
Automatic run skipped: check the PR author is skialpine, the PR is non-draft,
and the source paths match. For another author's PR, draft, or fork, skialpine
can request a review with a new standalone `@claude review` conversation comment.

To stop spending, disable **Claude PR review** from its Actions workflow menu.
Disable **Claude requested fix** separately to stop accepting fix requests.
Archiving the configured federation rule in Console prevents future token exchanges;
already-issued tokens remain subject to their expiry. Do not delete a shared
issuer/service account used by another workload.

Sources: [action setup](https://github.com/anthropics/claude-code-action/blob/main/docs/setup.md),
[action security](https://github.com/anthropics/claude-code-action/blob/main/docs/security.md),
[CLI limits/tools](https://code.claude.com/docs/en/cli-reference),
[models](https://platform.claude.com/docs/en/models/overview),
[Anthropic federation](https://platform.claude.com/docs/en/manage-claude/workload-identity-federation),
[rule matching](https://platform.claude.com/docs/en/manage-claude/wif-reference#rule-matching-semantics),
[GitHub OIDC claims](https://docs.github.com/en/actions/reference/security/oidc).
