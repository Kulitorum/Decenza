# debug_get_fds

Reads an in-process snapshot of every currently open file descriptor. Use it when logs suggest a
file-descriptor or socket leak, and take snapshots over time to compare the counts and individual
descriptors.

The tool takes no arguments and is read-only. It only supports Linux and Android, where the app can
read its own procfs entries. On other platforms it returns `supported: false` and an explanatory
`error`, rather than an empty census.

## Response fields

- `openFdCount` — number of descriptors in the snapshot.
- `descriptorKinds` — count by kind: `file`, `socket`, `pipe`, `anon_inode`, or `other`.
- `socketFdCount` — descriptors whose procfs target is a socket.
- `mappedSocketCount` — socket descriptors whose inode matched a procfs socket table entry.
- `socketProtocols` and `socketStates` — counts for mapped sockets, grouped by protocol and state.
- `descriptors` — one object per descriptor, ordered by descriptor number. Every object has `fd`,
  `kind`, and `target`. Socket entries also include `inode`; mapped internet sockets include
  `family`, `protocol`, `state`, `localAddress`, `localPort`, `remoteAddress`, and `remotePort`.
  Mapped Unix sockets include `family`, `protocol`, `state`, and, when available, `path`. A socket
  without a matching procfs table entry has `family: "unmapped"`.

Socket snapshots are momentary: connections can close or open while the census is being read, so a
small difference between aggregate counts and a subsequent snapshot is normal. Focus on repeated
growth across comparable snapshots, grouping by `target`, endpoint, protocol, and state.
