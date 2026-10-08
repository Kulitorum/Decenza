#!/usr/bin/env python3
"""Publish a maintainer-requested fix to its predetermined PR branch."""

import json
import os
from pathlib import Path
import subprocess
import sys


def run(*args, cwd=None):
    return subprocess.run(args, cwd=cwd, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


def publish():
    if len(sys.argv) != 1:
        raise ValueError('Publisher accepts no arguments.')
    workspace = Path(os.environ['GITHUB_WORKSPACE'])
    data_dir = Path(os.environ['RUNNER_TEMP']) / 'decenza-fix'
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    command = event['comment']['body'].splitlines()[0].strip()
    if command not in ('@claude fix', '@claude fix same-pr'):
        raise ValueError('Unsupported fix command.')
    repository = os.environ['GITHUB_REPOSITORY']
    number = event['issue']['number']
    original = json.loads((data_dir / 'pull-request.json').read_text())
    current = json.loads(run('gh', 'api', f'repos/{repository}/pulls/{number}'))
    default_branch = event['repository']['default_branch']
    source_branch = original['head']['ref']
    expected_sha = original['head']['sha']
    if (current['state'] != 'open' or current['head']['repo']['full_name'] != repository
            or current['head']['ref'] != source_branch
            or current['head']['sha'] != expected_sha
            or source_branch in (default_branch, current['base']['ref'])):
        raise ValueError('PR changed, closed, or targets a protected publishing destination.')

    checkout = workspace / 'fix-head'

    def git(*args):
        return run('git', '-c', 'core.hooksPath=/dev/null', '-c', 'core.fsmonitor=false',
                   '-c', 'commit.gpgsign=false', *args, cwd=checkout)

    if git('rev-parse', 'HEAD') != expected_sha:
        raise ValueError('Fix checkout is not the captured PR revision.')
    git('add', '--all', '--', '.')
    names = git('diff', '--cached', '--name-only', '--no-renames', '-z').split('\0')
    names = [name for name in names if name]
    if not names:
        raise ValueError('No fix changes to publish.')
    if len(names) > 20:
        raise ValueError('Fix exceeds 20 files; narrow the request.')
    allowed_roots = {'src', 'qml', 'tests', 'shaders', 'android', 'ios', 'macos',
                     'cmake', 'resources'}
    forbidden_parts = {'.git', '.github', '.claude', '.codex', '.agents'}
    forbidden_names = {'.mcp.json', '.claude.json', '.gitmodules', '.gitattributes',
                       'CLAUDE.md', 'CLAUDE.local.md', 'AGENTS.md'}
    for name in names:
        parts = Path(name).parts
        if (not parts or name.startswith('/') or '..' in parts
                or forbidden_parts.intersection(parts) or parts[-1] in forbidden_names
                or (parts[0] not in allowed_roots and name != 'CMakeLists.txt')):
            raise ValueError('Fix changes a path outside the permitted source scope.')
    for entry in git('ls-files', '--stage', '-z').split('\0'):
        if entry.startswith('120000 '):
            raise ValueError('Symlink-containing fix checkouts are unsupported.')

    # The lock and receipt live outside Claude's permitted edit tree.
    lock = data_dir / 'publish.lock'
    lock.touch(exist_ok=False)
    same_pr = command == '@claude fix same-pr'
    target = source_branch if same_pr else f'claude-fix/pr-{number}-run-{os.environ["GITHUB_RUN_ID"]}'
    git('check-ref-format', f'refs/heads/{target}')
    git('config', 'user.name', 'claude[bot]')
    git('config', 'user.email', '41898282+claude[bot]@users.noreply.github.com')
    git('commit', '-m', f'Fix review findings for PR #{number}')

    askpass = data_dir / 'git-askpass.sh'
    askpass.write_text('#!/bin/sh\ncase "$1" in\n*Username*) printf "%s\\n" x-access-token;;\n*) printf "%s\\n" "$GH_TOKEN";;\nesac\n')
    askpass.chmod(0o700)
    os.environ['GIT_ASKPASS'] = str(askpass)
    os.environ['GIT_TERMINAL_PROMPT'] = '0'
    git('remote', 'set-url', 'origin', f'https://github.com/{repository}.git')
    # A normal fast-forward push fails if the source branch gained newer commits.
    git('-c', 'credential.helper=', 'push', 'origin', f'HEAD:refs/heads/{target}')
    url = current['html_url']
    if not same_pr:
        body = data_dir / 'draft-body.md'
        body.write_text(f'Maintainer-requested fixes for {url}.\n\n'
                        'Targets the original PR branch so this draft shows only the fixes.\n\n'
                        'Validation: Qt build and tests have not run. Review the diff and run '
                        'the repository\'s Qt Creator test process before merging.\n')
        url = run('gh', 'pr', 'create', '--repo', repository, '--draft', '--base', source_branch,
                  '--head', target, '--title', f'Fix review findings for PR #{number}',
                  '--body-file', str(body))
    else:
        run('gh', 'pr', 'comment', str(number), '--repo', repository, '--body',
            f'Maintainer-requested fix pushed: {git("rev-parse", "HEAD")}. '
            'Qt build and tests have not run; validate before merging.')
    receipt = {'url': url, 'commit': git('rev-parse', 'HEAD'), 'mode': 'same-pr' if same_pr else 'draft'}
    (data_dir / 'published.json').write_text(json.dumps(receipt))
    print(f'Published requested fix: {url}')


if __name__ == '__main__':
    try:
        publish()
    except ValueError as error:
        sys.exit(str(error))
    except (KeyError, OSError, subprocess.CalledProcessError):
        # Subprocess errors can contain authenticated remote URLs; keep them out of logs.
        sys.exit('Fix publication failed. Inspect the branch before retrying; no force-push was used.')
