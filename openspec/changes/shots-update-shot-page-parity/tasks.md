## 1. Implementation

- [x] 1.1 `shots_update`: bagId, equipmentId, taste, storage dates; refusals with reasons; beanBase sets the indexed bean id
- [x] 1.2 Shot storage refuses a bag id that names no bag; taste value lists shared
- [x] 1.3 Bump McpSurfaceVersion; update MCP_SERVER.md

## 2. Verification

- [x] 2.1 Build and run the full suite; break-check the new test
- [ ] 2.2 HELD until the beta is on the tablet: correct the 45 Hometown (Sweet Bloom, Hometown Blend) shots from 2026-09-10 08:45 through 2026-10-07 10:05, all portion 1 (confirmed by the user), with `mcp__de1__shots_update`: frozen 2026-09-03, thawed 2026-09-09, opened 2026-09-10, vacuum-sealed
