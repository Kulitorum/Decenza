---
name: merge-pr
description: Squash-merge a GitHub PR and delete the branch. Use when the user says "merge", "merge the PR", "squash merge", or otherwise asks to land an open PR. This is the project-standard merge for Decenza.
---

# merge-pr

Project-standard PR merge for Decenza: **squash + delete branch**. Every merged PR lands as a single commit on `main` with the PR title as the commit subject and the PR description as the body. Feature branches are deleted on both the remote and locally.

## Steps

1. **Resolve the target PR.**
   - If the user gave a number, use it.
   - Otherwise, get the PR for the current branch:
     ```bash
     gh pr view --repo Kulitorum/Decenza --json number,headRefName,state,isDraft,mergeable,mergeStateStatus
     ```
   - If no PR exists for the current branch and no number was given, stop and tell the user.

2. **Pre-flight checks.**
   - State must be `OPEN`, `isDraft` must be `false`, `mergeable` must be `MERGEABLE`. If any fails, stop and report why.
   - Check CI: `gh pr checks <num> --repo Kulitorum/Decenza`. If checks are failing, stop and report. If checks are still running, ask the user whether to wait or merge anyway.
   - If the PR was opened in this session, no extra approval is needed. If the user is asking to merge a PR they did not open in this conversation, confirm first ("Merge PR #N now?").

3. **Merge.**
   ```bash
   gh pr merge <num> --repo Kulitorum/Decenza --squash --delete-branch
   ```
   Always pass both `--squash` and `--delete-branch`. Never use `--merge` or `--rebase` — Decenza standardizes on squash.

4. **Verify and report.**
   ```bash
   gh pr view <num> --repo Kulitorum/Decenza --json state,mergedAt,mergeCommit
   ```
   Report the merge commit SHA as a clickable link: `[<short-sha>](https://github.com/Kulitorum/Decenza/commit/<full-sha>)`. Mention any auto-closed issues from the PR description.

5. **Local cleanup (if the user is on the merged branch).**
   - If the current branch matches the PR's `headRefName`, switch back to `main`, pull, and delete the local branch:
     ```bash
     git checkout main && git pull && git branch -D <branch>
     ```
   - Otherwise leave the local working copy alone.

## When NOT to use

- PR is a draft, has failing required checks, or has unresolved review threads — surface the blocker, don't merge.
- PR targets a non-`main` branch (release branch, hotfix branch) — confirm with the user before squashing.
- User explicitly asked for a different merge strategy (`--merge` for a true merge commit, `--rebase` for rebase) — follow the user's instruction and note that it deviates from project standard.
