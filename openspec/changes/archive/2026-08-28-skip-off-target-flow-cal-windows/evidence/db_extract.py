#!/usr/bin/env python3
"""Extract shot curves from a Decenza shots.db into the shape flowcal_sim.py reads.

Sample blobs are qCompress()'d JSON: a 4-byte big-endian uncompressed length
followed by a zlib stream (shothistorystorage.cpp:2169). Each series inside is
{"t": [...], "v": [...]}.

Usage: db_extract.py <shots.db> <out-dir> [profile-name-substring] [limit]
"""

import json
import os
import sqlite3
import sys
import zlib


def blob_to_series(blob):
    payload = zlib.decompress(bytes(blob)[4:])
    return json.loads(payload)


def series(obj, key):
    part = obj.get(key) or {}
    t, v = part.get("t", []), part.get("v", [])
    return [{"x": a, "y": b} for a, b in zip(t, v)]


def main(db_path, out_dir, name_filter="", limit=40):
    os.makedirs(out_dir, exist_ok=True)
    con = sqlite3.connect("file:%s?mode=ro" % db_path, uri=True)
    con.row_factory = sqlite3.Row
    q = ("SELECT s.id, s.timestamp, s.profile_name, s.profile_json, s.duration_seconds, "
         "s.final_weight, s.dose_weight, b.data_blob "
         "FROM shots s JOIN shot_samples b ON b.shot_id = s.id "
         "WHERE s.profile_name LIKE ? ORDER BY s.timestamp DESC LIMIT ?")
    written = 0
    for row in con.execute(q, ("%" + name_filter + "%", limit * 4)):
        try:
            data = blob_to_series(row["data_blob"])
        except Exception as exc:                      # corrupt / legacy blob
            print("skip %s: %s" % (row["id"], exc))
            continue
        wf = series(data, "weightFlowRate")
        if len(wf) < 20:                              # no usable scale data
            continue
        shot = {
            "id": row["id"],
            "timestampIso": row["timestamp"],
            "pressure": series(data, "pressure"),
            "flow": series(data, "flow"),
            "flowGoal": series(data, "flowGoal"),
            "weightFlowRate": wf,
            "phases": [],
            "profileJson": row["profile_json"] or "{}",
            "profileName": row["profile_name"],
            "durationSec": row["duration_seconds"],
            "finalWeightG": row["final_weight"],
        }
        with open(os.path.join(out_dir, "shot_%s.json" % row["id"]), "w") as fh:
            json.dump(shot, fh)
        written += 1
        if written >= limit:
            break
    print("wrote %d shot(s) to %s" % (written, out_dir))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2],
         sys.argv[3] if len(sys.argv) > 3 else "",
         int(sys.argv[4]) if len(sys.argv) > 4 else 40)
