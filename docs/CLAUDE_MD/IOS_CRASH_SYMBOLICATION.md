# Symbolicating an iOS crash report

Auto-reported iOS crashes arrive as GitHub issues with stripped backtraces. Frames 0-1 name
`QtPrivate::MetaObjectForType<QBluetoothPermission>::metaObjectFunction + <huge offset>`: that is
the nearest exported symbol, not the crash. Frame 0 is really `CrashHandler::writeCrashLog`,
frame 1 `CrashHandler::signalHandler`, and the crash site is frame 3.

## Steps

1. **Find the run.** dSYMs are uploaded only by tag pushes of `release.yml` (`build-ios`), as the
   `Decenza-iOS-dSYMs` artifact, kept 90 days. Match the report's `built <date> <time>` against
   the run's `createdAt`, allowing ~10 minutes; run titles rarely match the branch.
   ```bash
   gh run list --repo Kulitorum/Decenza --workflow release.yml --event push --limit 50 \
     --json databaseId,createdAt,headBranch,displayTitle
   gh run download <run-id> --repo Kulitorum/Decenza -n Decenza-iOS-dSYMs -D <dir>
   ```
2. **Recover the ASLR slide** from one anchor frame: subtract its reported `+N` from its runtime
   address, then subtract the symbol's address in the dSYM.
   ```bash
   DSYM=<dir>/Decenza.app.dSYM/Contents/Resources/DWARF/Decenza
   nm -n "$DSYM" | grep -E 'MetaObjectForTypeI(20QBluetoothPermission|19QLocationPermission)v'
   # slide = (frame_addr - reported_offset) - nm_addr
   ```
   The slide must be a multiple of `0x4000`, and two anchors must agree.
3. **Symbolicate with the load address `0x100000000 + slide`.** Passing the bare slide, or
   `0x100000000`, makes `atos` echo the addresses back, which looks like missing symbols.
   ```bash
   atos -o "$DSYM" -arch arm64 -l 0x10240C000 <frame addresses>
   ```
4. **Subtract 1 from every frame address except #0.** They are return addresses and `atos` does
   not adjust them; without it a call that ends a function resolves to the next function at `+0`,
   which is impossible and is the tell.

## Reading the result

- **Names can lie.** The link folds identical code (thousands of aliased addresses per build).
  Check whether a suspicious address carries more than one symbol in `nm`, and distrust a frame
  whose caller makes no sense. The dSYM has no `__text`, so it cannot be disassembled.
- **An exit-time abort is not a crash the user saw.** A stack ending in
  `exit() -> __cxa_finalize_ranges` aborted during static destruction after a polite termination
  (swipe-kill or OS reclaim; a jetsam kill is `SIGKILL` and leaves no log). #1777 and #1958 are the
  same Qt `QMetaTypeFunctionRegistry::remove` abort for the built-in `QList<QVector3D>` converter,
  probably racing a live thread; no Decenza code is in the path.
