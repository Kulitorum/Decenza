## 1. Source identity

- [x] 1.1 Name a tunnel-proxied caller `Funnel (public internet)` instead of its loopback peer address, in one place that serves both the log line and the failed-token limiter key.
- [x] 1.2 Thread that label from the connector into `McpServer` so the modern era's control-call limiter keys and reports the same name. It cannot derive it: only the listener knows whether it is tunnel-proxied.
- [x] 1.3 Keep the raw peer address in Mode C, where it is a genuine peer.

## 2. Refusal policy

- [x] 2.1 One function for the reply given to a caller that has not proved it knows the token; route all four refusal sites through it.
- [x] 2.2 Behind an embedded tunnel: no reply, socket closed. Mode C: bare 404, keep-alive intact.
- [x] 2.3 Leave the post-authorization 404s alone — their caller already knows the service is there.

## 3. Log budget

- [x] 3.1 Drop `MaxFailedPerMinute` from 20 to 3.
- [x] 3.2 After the budget, log one transition line and then only decimal milestones, each carrying the running count.
- [x] 3.3 Add `McpRateWindow::countInWindow()` for the milestone decision.

## 4. Tests

- [x] 4.1 Tunnel-mode listener: the warning names `Funnel (public internet)`, a bad token gets no reply, malformed framing gets no reply, a valid token still works.
- [x] 4.2 Count the warnings the real listener emits under sustained rejection; assert fewer lines than requests.
- [x] 4.3 Pin the budget small, so raising it back is a deliberate act.
- [x] 4.4 Modern control-limiter refusal names the connector-supplied caller.
- [x] 4.5 `readResponse` in the connector test breaks on a no-reply disconnect instead of waiting out its timeout.

## 5. Docs

- [x] 5.1 Delta spec for the changed refusal requirement (no direct edit of `openspec/specs`).
- [ ] 5.2 No wiki manual change: nothing here is visible on screen — the connector URL, token and rotation flow are unchanged.
