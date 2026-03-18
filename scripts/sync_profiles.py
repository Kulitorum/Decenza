#!/usr/bin/env python3
"""
Sync Decenza JSON profiles to match de1app TCL source profiles.

Updates profile-level settings and per-frame exit conditions/limiters
to match de1app values. Only modifies profiles that have a matching
title in de1app. Decenza-only profiles are left unchanged.

Usage:
    python scripts/sync_profiles.py [de1app_profiles_dir]

    de1app_profiles_dir defaults to ~/Documents/GitHub/de1app/de1plus/profiles/
"""

import json
import re
import sys
from pathlib import Path

DECENZA_DIR = Path(__file__).resolve().parent.parent / "resources" / "profiles"

if len(sys.argv) > 1:
    DE1APP_DIR = Path(sys.argv[1])
else:
    DE1APP_DIR = Path.home() / "Documents" / "GitHub" / "de1app" / "de1plus" / "profiles"

if not DE1APP_DIR.exists():
    print(f"Error: de1app profiles directory not found: {DE1APP_DIR}")
    print(f"Usage: {sys.argv[0]} [de1app_profiles_dir]")
    sys.exit(1)


def parse_tcl_profile(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()
    profile = {}
    m = re.search(r'profile_title\s+\{([^}]*)\}', content)
    if m:
        profile['title'] = m.group(1).strip()
    else:
        m = re.search(r'profile_title\s+(\S+)', content)
        if m:
            profile['title'] = m.group(1).strip()
    m = re.search(r'settings_profile_type\s+(\S+)', content)
    if m:
        profile['profile_type'] = m.group(1).strip()

    # Extract profile-level numeric settings
    NUMERIC_KEYS = [
        'final_desired_shot_weight_advanced',
        'final_desired_shot_volume_advanced',
        'espresso_temperature',
        'tank_desired_water_temperature',
        'maximum_pressure',
        'maximum_flow',
        'maximum_pressure_range_advanced',
        'maximum_flow_range_advanced',
        'final_desired_shot_volume_advanced_count_start',
        'espresso_temperature_0',
        'espresso_temperature_1',
        'espresso_temperature_2',
        'espresso_temperature_3',
        'espresso_temperature_steps_enabled',
        # Simple pressure profile (settings_2a)
        'espresso_pressure',
        'espresso_decline_time',
        'espresso_hold_time',
        'preinfusion_time',
        'preinfusion_stop_pressure',
        'preinfusion_flow_rate',
        'pressure_end',
        # Simple flow profile (settings_2b)
        'flow_profile_preinfusion',
        'flow_profile_preinfusion_time',
        'flow_profile_hold',
        'flow_profile_hold_time',
        'flow_profile_decline',
        'flow_profile_decline_time',
        'flow_profile_minimum_pressure',
    ]
    for key in NUMERIC_KEYS:
        m = re.search(rf'{key}\s+([0-9.]+)', content)
        if m:
            profile[key] = float(m.group(1))

    # Extract string settings
    m = re.search(r'beverage_type\s+(\S+)', content)
    if m:
        profile['beverage_type'] = m.group(1)

    # Parse advanced_shot frames
    idx = content.find('advanced_shot ')
    frames = []
    if idx >= 0:
        i = idx + len('advanced_shot ')
        while i < len(content) and content[i] in ' \t':
            i += 1
        if i < len(content) and content[i] == '{':
            depth = 0
            start = i
            for j in range(i, len(content)):
                if content[j] == '{':
                    depth += 1
                elif content[j] == '}':
                    depth -= 1
                    if depth == 0:
                        fs = content[start + 1:j].strip()
                        if fs:
                            frames = parse_tcl_frames(fs)
                        break
    profile['frames'] = frames
    return profile


def parse_tcl_frames(s):
    frames = []
    depth = 0
    cur = []
    for c in s:
        if c == '{':
            if depth > 0:
                cur.append(c)
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                fs = ''.join(cur).strip()
                if fs:
                    frames.append(parse_tcl_frame(fs))
                cur = []
            elif depth > 0:
                cur.append(c)
        else:
            if depth > 0:
                cur.append(c)
    return frames


def parse_tcl_frame(s):
    f = {}
    toks = []
    i = 0
    while i < len(s):
        if s[i] in ' \t\n\r':
            i += 1
            continue
        if s[i] == '{':
            d = 1
            j = i + 1
            while j < len(s) and d > 0:
                if s[j] == '{':
                    d += 1
                elif s[j] == '}':
                    d -= 1
                j += 1
            toks.append(s[i + 1:j - 1])
            i = j
        else:
            j = i
            while j < len(s) and s[j] not in ' \t\n\r':
                j += 1
            toks.append(s[i:j])
            i = j
    for k in range(0, len(toks) - 1, 2):
        f[toks[k]] = toks[k + 1]
    return f


def tcl_frame_to_json_exit(tf):
    """Convert TCL frame exit fields to Decenza JSON exit object."""
    exit_if = int(tf.get('exit_if', 0))
    if not exit_if:
        return None

    exit_type = tf.get('exit_type', '')
    # exit_type is like "pressure_over" -> type="pressure", condition="over"
    parts = exit_type.rsplit('_', 1) if exit_type else ['', '']
    if len(parts) == 2:
        etype, econd = parts
    else:
        return None

    value = 0
    if exit_type == 'pressure_over':
        value = float(tf.get('exit_pressure_over', 0))
    elif exit_type == 'pressure_under':
        value = float(tf.get('exit_pressure_under', 0))
    elif exit_type == 'flow_over':
        value = float(tf.get('exit_flow_over', 0))
    elif exit_type == 'flow_under':
        value = float(tf.get('exit_flow_under', 0))

    return {'type': etype, 'condition': econd, 'value': value}


def tcl_frame_to_json_limiter(tf):
    """Convert TCL frame limiter fields to Decenza JSON limiter object."""
    value = float(tf.get('max_flow_or_pressure', 0))
    range_val = float(tf.get('max_flow_or_pressure_range', 0.6))
    if value <= 0 and abs(range_val - 0.6) < 0.001:
        return None  # Default values, no limiter needed
    return {
        'value': value,
        'range': range_val
    }


def sync_frame(jf, tf):
    """Update a Decenza JSON frame to match de1app TCL frame. Returns True if changed."""
    changed = False

    # Sync core frame values
    CORE_FLOAT = ['pressure', 'flow', 'temperature', 'seconds', 'volume']
    CORE_STR = ['name', 'pump', 'transition', 'sensor']

    for key in CORE_FLOAT:
        tv = float(tf.get(key, 0))
        jv = float(jf.get(key, 0))
        if abs(jv - tv) > 0.001:
            jf[key] = tv
            changed = True

    for key in CORE_STR:
        tv = tf.get(key, '')
        jv = jf.get(key, '')
        if jv != tv:
            jf[key] = tv
            changed = True

    # Sync weight
    tv = float(tf.get('weight', 0))
    jv = float(jf.get('weight', 0))
    if abs(jv - tv) > 0.001:
        jf['weight'] = tv
        changed = True

    # Sync exit condition
    tcl_exit = tcl_frame_to_json_exit(tf)
    json_exit = jf.get('exit')
    if tcl_exit and not json_exit:
        jf['exit'] = tcl_exit
        changed = True
    elif not tcl_exit and json_exit:
        del jf['exit']
        changed = True
    elif tcl_exit and json_exit:
        if (json_exit.get('type') != tcl_exit['type'] or
            json_exit.get('condition') != tcl_exit['condition'] or
            abs(float(json_exit.get('value', 0)) - tcl_exit['value']) > 0.001):
            jf['exit'] = tcl_exit
            changed = True

    # Sync limiter
    tcl_lim = tcl_frame_to_json_limiter(tf)
    json_lim = jf.get('limiter')
    # Treat empty limiter object {} as absent
    if json_lim is not None and not json_lim.get('value'):
        json_lim = None
    if tcl_lim and not json_lim:
        jf['limiter'] = tcl_lim
        changed = True
    elif not tcl_lim and json_lim:
        del jf['limiter']
        changed = True
    elif tcl_lim and json_lim:
        if (abs(float(json_lim.get('value', 0)) - tcl_lim['value']) > 0.001 or
            abs(float(json_lim.get('range', 0.6)) - tcl_lim['range']) > 0.001):
            jf['limiter'] = tcl_lim
            changed = True

    return changed


def sync_profile(jdata, tdata):
    """Update a Decenza JSON profile to match de1app TCL profile. Returns list of changes."""
    changes = []

    # Sync profile-level settings
    # Map: (Decenza JSON key, TCL key, default when absent from TCL)
    SETTINGS_MAP = [
        ('target_weight', 'final_desired_shot_weight_advanced', 0.0),
        ('target_volume', 'final_desired_shot_volume_advanced', 0.0),
        ('espresso_temperature', 'espresso_temperature', None),
        ('tank_desired_water_temperature', 'tank_desired_water_temperature', 0.0),
        ('maximum_pressure', 'maximum_pressure', 0.0),
        ('maximum_flow', 'maximum_flow', 0.0),
        ('maximum_pressure_range_advanced', 'maximum_pressure_range_advanced', 0.6),
        ('maximum_flow_range_advanced', 'maximum_flow_range_advanced', 0.6),
        ('beverage_type', 'beverage_type', None),
        # Simple pressure profile (settings_2a)
        ('espresso_pressure', 'espresso_pressure', None),
        ('espresso_decline_time', 'espresso_decline_time', None),
        ('espresso_hold_time', 'espresso_hold_time', None),
        ('preinfusion_time', 'preinfusion_time', None),
        ('preinfusion_stop_pressure', 'preinfusion_stop_pressure', None),
        ('preinfusion_flow_rate', 'preinfusion_flow_rate', None),
        ('pressure_end', 'pressure_end', None),
        # Simple flow profile (settings_2b)
        ('flow_profile_preinfusion', 'flow_profile_preinfusion', None),
        ('flow_profile_preinfusion_time', 'flow_profile_preinfusion_time', None),
        ('flow_profile_hold', 'flow_profile_hold', None),
        ('flow_profile_hold_time', 'flow_profile_hold_time', None),
        ('flow_profile_decline', 'flow_profile_decline', None),
        ('flow_profile_decline_time', 'flow_profile_decline_time', None),
        ('flow_profile_minimum_pressure', 'flow_profile_minimum_pressure', None),
    ]

    for jkey, tkey, default in SETTINGS_MAP:
        if tkey not in tdata:
            if default is None:
                continue
            tv = default  # Use de1app default when key is absent from TCL
        else:
            tv = tdata[tkey]
        jv = jdata.get(jkey)
        if isinstance(tv, float):
            if jv is None or abs(float(jv) - tv) > 0.001:
                changes.append(f"  {jkey}: {jv} -> {tv}")
                jdata[jkey] = tv
        elif isinstance(tv, str):
            if jv != tv:
                changes.append(f"  {jkey}: {jv} -> {tv}")
                jdata[jkey] = tv

    # Sync frames
    jframes = jdata.get('steps', [])
    tframes = tdata.get('frames', [])

    if len(jframes) != len(tframes):
        changes.append(f"  FRAME COUNT MISMATCH: D={len(jframes)}, T={len(tframes)} — skipping frame sync")
        return changes

    for i in range(len(jframes)):
        if sync_frame(jframes[i], tframes[i]):
            changes.append(f"  Frame {i} ({jframes[i].get('name', f'frame {i}')}): updated")

    # Remove stop_at_type if present
    if 'stop_at_type' in jdata:
        changes.append(f"  Removed stop_at_type")
        del jdata['stop_at_type']

    return changes


# Build de1app index by title
de1app = {}
for f in sorted(DE1APP_DIR.glob("*.tcl")):
    try:
        data = parse_tcl_profile(f)
        title = data.get('title', f.stem)
        de1app[title.lower()] = {'file': f.name, 'data': data, 'title': title}
    except Exception:
        pass

# Process each Decenza profile
updated = 0
skipped = 0
unmatched = 0

for f in sorted(DECENZA_DIR.glob("*.json")):
    try:
        jdata = json.load(open(f))
    except Exception:
        continue

    title = jdata.get('title', '').lower()
    if title not in de1app:
        unmatched += 1
        continue

    tdata = de1app[title]['data']
    changes = sync_profile(jdata, tdata)

    if changes:
        with open(f, 'w') as fh:
            json.dump(jdata, fh, indent=4)
            fh.write('\n')
        updated += 1
        print(f"\n{jdata.get('title', '')} ({f.name})")
        for c in changes:
            print(c)
    else:
        skipped += 1

print(f"\n{'=' * 60}")
print(f"Updated: {updated}")
print(f"Already in sync: {skipped}")
print(f"Decenza-only (no de1app match): {unmatched}")
