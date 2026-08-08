## Context

See `proposal.md` — Why. Three constraints shape the whole approach:

1. **The canonical database is read-only to us and does not contain every roaster.** For the reported user, `q=Stavanger` returns 0 rows. There is no "get the right id" option; the only correct state for a borrowed link is unlinked.
2. **Shot snapshots are historical records.** Each shot stores its own copy of the bean blob, and the upload path reads that copy rather than the bag. So a bag-level fix that does not reach the shots changes nothing, and a shot's local bean fields must not be rewritten to match a later bag rename.
3. **The damage already exists in the cloud.** Fixing storage stops new damage; the shots visualizer.coffee already renamed stay renamed until something writes to them.

## Goals / Non-Goals

**Goals:**

- One enforcement point for the identity invariant, so every editing surface inherits it.
- Repair existing cloud damage without a library-wide comparison and without exceeding the server's published rate limits.
- Never make a write whose only possible outcome is no change, and never claim a repair that did not happen.

**Non-Goals:**

- Correcting the canonical database, or finding a "better" id for a bag whose roaster is absent from it.
- Rewriting shot snapshots to match a bag rename — a shot records what was true when it was pulled.
- Clearing the borrowed link from the user's server-side coffee *bag*. The bag push omits empty local values rather than sending null, deliberately, so it cannot blank a value the user set on visualizer.coffee.
- Telling the user their bag was unlinked. The event is logged; a toast for a correction they did not ask for was considered and declined.

## Decisions

**Enforce at the storage write, not in the editor.** The mismatch can be created in one save by three different surfaces (bag editor, MCP `bag_update`, web `/beans` editor). A check in each is three copies free to drift; a check at the write is inherited by all of them, and there is no fourth writer to the canonical columns. Alternative considered: gate only the export. Rejected — the wrong id stays in the database, every future surface has to remember the gate, and the user's own data keeps a false claim in it.

**Compare the record's pristine names, not the working ones.** The blob's `roasterName`/`roastName` are user-editable, so after the user corrects the roaster they no longer describe the record. The `canonical` snapshot captured on first edit is what the record actually said. Without this the predicate compares the bag against itself and never fires.

**An empty name proves nothing.** Legacy blobs stored no names. Treating unknown as a conflict would unlink working bags wholesale, so only a populated disagreement counts — but this is one-directional: the same rule applied to a WRITE means an empty local name must never be sent, because that blanks a real value on the server.

**A recorded queue, not a scan.** The first implementation compared the paged shot list against local rows; that endpoint omits the bean fields, the missing keys read back as empty strings, and the pass queued an entire library and then collected rate-limit refusals on every request. Recording the cohort at the moment of the unlink is exact, needs no comparison, and is bounded by construction.

**Pace by request, from the server's published limits.** 50/min per IP and 200/10 min per IP and per user, shared with shot upload. The interval is derived from those numbers rather than chosen, and it gates every request — a shot that needs a write costs two, so pacing per shot understates the real rate by half.

**Read before writing; read back after.** The list cannot answer what a shot holds, and a success status does not prove the values took — the server may overwrite them from a bag between the request and the save, and it answers with the assigned attributes either way. Both directions of that were found by watching a live account rather than by reading the code.

**Decide what to write from the read, not from the local row.** Whether a shot has a server-side bag is only knowable after reading it, and it is the fact that decides whether any write can survive. Deciding earlier is what made an earlier version send a request whose sole effect was to re-render the user's roast date.

**Fail closed on every read that licenses a write.** An unparseable body, a missing bean field, or a bag id that is present but not a string all mean "we do not know", and the shot stays queued. The alternative — treating an unreadable field as an absence — turns a proxy error page into permission to write.

## Risks / Trade-offs

- **The pass writes to a user's cloud account.** → Every write is preceded by a read, followed by a read-back, restricted to shots recorded at an unlink, and paced under the server's limits. Verified end to end against a live account.
- **A conservative predicate leaves some bad links in place.** A blob that no longer carries the record's names cannot be proven conflicted. → Accepted: the wrong answer in that direction is inaction, and the export gate still withholds anything it cannot verify.
- **An aggressive predicate would unlink good bags at scale, and the migration would then queue their shots for cloud writes.** → The negative cases ("a consistent bag keeps its link") are pinned by tests on both write paths and on the migration.
- **A large affected library takes multiple launches.** At the paced rate, hundreds of shots exceed one session. → Accepted by design: the flags are durable, the pass resumes, and an abandoned pass loses nothing.
- **A shot declined because it has a server bag has its flag cleared while its cloud record may still read wrong.** → Accepted: nothing sent to that shot can survive, so no future pass could do better; the log says so and names the bag as what to correct.

## Migration Plan

One additive column and one data pass, run inside `initialize()` before any reader can observe the database. The unlink is the correctness fact and lands even if the column could not be added — only the cloud repair is lost. Where the column's existence cannot be determined, the migration defers rather than unlinking without queueing, because the version stamp is permanent and would retire the repair for that database forever.

Rollback is by database restore. There is no down-migration: the unlink is a correction, and re-attaching a borrowed record would restore the defect.
