# Contributing to Decenza

Contributions are welcome. Open a pull request against `main`; if you're unsure a change fits, open a draft early and ask.

## One concern per pull request

Each pull request does one thing a reviewer can judge on its own: one fix, one feature or one refactor.

- A small cleanup or pre-existing fix in files the pull request already touches may ride along. Name it in the description.
- Anything else you find along the way goes in its own pull request.
- If your change needs another one first, land that separately and write `Depends on #N` in yours. Don't stack a pull request on another's unmerged branch.

## Before you open it

- Build and run the full test suite ([TESTING.md](docs/CLAUDE_MD/TESTING.md)). A fix comes with a test that fails without it, or the pull request says why it can't have one.
- If you use Claude Code or Codex, run the `audit-code` skill on your diff, fix every Critical and High finding, and post the final run's report as a comment on the pull request; otherwise, ask for an audit in it. [CLAUDE.md](CLAUDE.md) holds the rules coding agents follow here.
- A user-visible change also updates the [manual](https://github.com/Kulitorum/Decenza/wiki/Manual).

## After you open it

Codex reviews every pull request automatically when it is opened, and leaves its findings as inline threads, each badged P1, P2 or P3. Read them. For every P1 and P2, fix it or reply on the thread with why it doesn't hold, then resolve the thread. A pull request merges only when all of its P1 and P2 threads show as resolved and Codex has reviewed its latest commit. Codex may skip a push it judges minor: if it hasn't started within 10 minutes, don't wait for it.
