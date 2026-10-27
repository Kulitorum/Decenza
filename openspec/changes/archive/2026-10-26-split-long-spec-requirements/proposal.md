# Proposal

## Why

`openspec validate --specs --strict` fails on two main specs because one requirement in each states several behaviours in a description over 500 characters. A long requirement hides which behaviour a scenario checks, and keeps strict validation red for every change.

## What Changes

- `mcp-server`: "Reachability Mode — Embedded Tailscale Funnel" keeps the node and Funnel behaviour; its sign-out (forget) behaviour becomes its own requirement.
- `weight-timed-steaming`: "Capture attempts do not tare" keeps the no-tare and net-milk behaviour; learning the empty reading, and discarding it, become two requirements.

No behaviour changes. Every existing scenario stays under its original requirement.

## Capabilities

### New Capabilities

None.

### Modified Capabilities
- `mcp-server`: one requirement split into two, same meaning.
- `weight-timed-steaming`: one requirement split into three, same meaning.

## Impact

Spec text only. No code, tests or user-visible behaviour change.
