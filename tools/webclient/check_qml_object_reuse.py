#!/usr/bin/env python3
"""Snapshot or verify qmlcache object files without changing the build tree."""

import argparse
import json
from pathlib import Path
import sys


def scan_objects(build_dir: Path) -> dict[str, dict[str, int]]:
    target_dir = build_dir / "CMakeFiles" / "Decenza.dir"
    if not target_dir.is_dir():
        raise ValueError(f"Expected target object directory does not exist: {target_dir}")

    objects: dict[str, dict[str, int]] = {}
    for path in target_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {".o", ".obj"}:
            continue
        relative = path.relative_to(build_dir).as_posix()
        if "/qmlcache/" not in f"/{relative}/":
            continue
        stat = path.stat()
        objects[relative] = {"mtime_ns": stat.st_mtime_ns, "size": stat.st_size}

    if not objects:
        raise ValueError(f"No .o/.obj files under {target_dir}/**/qmlcache/; object path layout may have changed")
    return dict(sorted(objects.items()))


def snapshot(build_dir: Path, json_path: Path) -> int:
    objects = scan_objects(build_dir)
    snapshot_data = {"version": 1, "objects": objects}
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(snapshot_data, indent=2) + "\n", encoding="utf-8")
    print(f"Snapshot recorded: {len(objects)} qmlcache object files")
    return 0


def check(build_dir: Path, json_path: Path) -> int:
    if not json_path.is_file():
        print(f"Snapshot file does not exist: {json_path}", file=sys.stderr)
        return 2
    try:
        saved = json.loads(json_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"Could not read snapshot {json_path}: {error}", file=sys.stderr)
        return 2
    if (not isinstance(saved, dict) or saved.get("version") != 1
            or not isinstance(saved.get("objects"), dict) or not saved["objects"]):
        print("Snapshot has an unsupported version or no qmlcache objects", file=sys.stderr)
        return 2

    try:
        current = scan_objects(build_dir)
    except (OSError, ValueError) as error:
        print(f"Could not scan qmlcache objects: {error}", file=sys.stderr)
        return 2

    prior = saved["objects"]
    prior_paths = set(prior)
    current_paths = set(current)
    missing = prior_paths - current_paths
    new = current_paths - prior_paths
    changed = {
        path for path in prior_paths & current_paths
        if prior[path] != current[path]
    }

    print(
        f"qmlcache object check: {len(current)} current, {len(prior)} saved, "
        f"{len(changed)} changed, {len(missing)} missing, {len(new)} new"
    )
    if changed or missing or new:
        for label, paths in (("changed", changed), ("missing", missing), ("new", new)):
            for path in sorted(paths):
                print(f"{label}: {path}")
        return 1
    print("All qmlcache object paths, timestamps, and sizes were reused unchanged")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("snapshot", "check"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("build_dir", type=Path)
        subparser.add_argument("json_path", type=Path)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    json_path = args.json_path.resolve()
    if args.command == "snapshot":
        try:
            return snapshot(build_dir, json_path)
        except (OSError, ValueError) as error:
            print(f"Could not record qmlcache object snapshot: {error}", file=sys.stderr)
            return 2
    return check(build_dir, json_path)


if __name__ == "__main__":
    raise SystemExit(main())
