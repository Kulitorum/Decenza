# Tasks

## 1. Split the requirements

- [x] 1.1 Split "Reachability Mode — Embedded Tailscale Funnel" (`mcp-server`) and "Capture attempts do not tare" (`weight-timed-steaming`), keeping every scenario under its original requirement. Verify: `openspec validate split-long-spec-requirements --strict` passes, and after archive `openspec validate --specs --strict` reports no failures.
