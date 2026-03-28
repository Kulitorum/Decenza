#!/usr/bin/env python3
"""
Compare Decenza JSON profiles against de1app TCL source profiles.

Classifies differences into severity categories:
1. MAJOR: Different frame counts, different core values (pressure, flow, temp, pump, etc.)
2. EXIT-CONDITION-ONLY: Decenza has exit_if=0 where de1app has exit_if=1 (or vice versa),
   but core frame parameters are identical
3. PERFECT/COSMETIC: No meaningful differences

Usage:
    python scripts/compare_profiles.py [de1app_profiles_dir]

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

    # Extract all profile-level settings
    # Map TCL key -> normalized key, with default value
    tcl_settings = {
        # Common settings
        'final_desired_shot_weight_advanced': ('target_weight', 0.0),
        'final_desired_shot_volume_advanced': ('target_volume', 0.0),
        'espresso_temperature': ('espresso_temperature', 0.0),
        'tank_desired_water_temperature': ('tank_temperature', 0.0),
        'maximum_pressure': ('maximum_pressure', 0.0),
        'maximum_flow': ('maximum_flow', 0.0),
        'maximum_pressure_range_advanced': ('maximum_pressure_range', 0.6),
        'maximum_flow_range_advanced': ('maximum_flow_range', 0.6),
        'beverage_type': ('beverage_type', 'espresso'),
        'settings_profile_type': ('profile_type', ''),
        'final_desired_shot_volume_advanced_count_start': ('volume_count_start', 0.0),
        'espresso_temperature_0': ('temperature_0', 0.0),
        'espresso_temperature_1': ('temperature_1', 0.0),
        'espresso_temperature_2': ('temperature_2', 0.0),
        'espresso_temperature_3': ('temperature_3', 0.0),
        'espresso_temperature_steps_enabled': ('temperature_steps_enabled', 0),
        # Simple pressure profile (settings_2a) settings
        'espresso_pressure': ('espresso_pressure', 0.0),
        'espresso_decline_time': ('espresso_decline_time', 0.0),
        'espresso_hold_time': ('espresso_hold_time', 0.0),
        'preinfusion_time': ('preinfusion_time', 0.0),
        'preinfusion_stop_pressure': ('preinfusion_stop_pressure', 0.0),
        'preinfusion_flow_rate': ('preinfusion_flow_rate', 0.0),
        'pressure_end': ('pressure_end', 0.0),
        # Simple flow profile (settings_2b) settings
        'flow_profile_preinfusion': ('flow_profile_preinfusion', 0.0),
        'flow_profile_preinfusion_time': ('flow_profile_preinfusion_time', 0.0),
        'flow_profile_hold': ('flow_profile_hold', 0.0),
        'flow_profile_hold_time': ('flow_profile_hold_time', 0.0),
        'flow_profile_decline': ('flow_profile_decline', 0.0),
        'flow_profile_decline_time': ('flow_profile_decline_time', 0.0),
        'flow_profile_minimum_pressure': ('flow_profile_minimum_pressure', 0.0),
    }
    for tcl_key, (norm_key, default) in tcl_settings.items():
        if isinstance(default, float):
            m = re.search(rf'{tcl_key}\s+([0-9.]+)', content)
            profile[norm_key] = float(m.group(1)) if m else default
        elif isinstance(default, int):
            m = re.search(rf'{tcl_key}\s+([0-9]+)', content)
            profile[norm_key] = int(m.group(1)) if m else default
        else:
            m = re.search(rf'{tcl_key}\s+(\S+)', content)
            profile[norm_key] = m.group(1) if m else default

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


def norm_tcl(f):
    return {
        'name': f.get('name', ''),
        'pump': f.get('pump', 'pressure'),
        'pressure': float(f.get('pressure', 0)),
        'flow': float(f.get('flow', 0)),
        'temperature': float(f.get('temperature', 0)),
        'seconds': float(f.get('seconds', 0)),
        'volume': float(f.get('volume', 0)),
        'transition': f.get('transition', 'fast'),
        'sensor': f.get('sensor', 'coffee'),
        'exit_if': int(f.get('exit_if', 0)),
        'exit_type': f.get('exit_type', ''),
        'exit_pressure_over': float(f.get('exit_pressure_over', 0)),
        'exit_pressure_under': float(f.get('exit_pressure_under', 0)),
        'exit_flow_over': float(f.get('exit_flow_over', 0)),
        'exit_flow_under': float(f.get('exit_flow_under', 0)),
        'max_flow_or_pressure': float(f.get('max_flow_or_pressure', 0)),
        'max_flow_or_pressure_range': float(f.get('max_flow_or_pressure_range', 0.6)),
        'weight': float(f.get('weight', 0)),
    }


def norm_json(f):
    n = {
        'name': f.get('name', ''),
        'pump': f.get('pump', 'pressure'),
        'pressure': float(f.get('pressure', 0)),
        'flow': float(f.get('flow', 0)),
        'temperature': float(f.get('temperature', 0)),
        'seconds': float(f.get('seconds', 0)),
        'volume': float(f.get('volume', 0)),
        'transition': f.get('transition', 'fast'),
        'sensor': f.get('sensor', 'coffee'),
    }
    ex = f.get('exit')
    if ex:
        n['exit_if'] = 1
        et = ex.get('type', '') + '_' + ex.get('condition', '')
        n['exit_type'] = et
        ev = float(ex.get('value', 0))
        n['exit_pressure_over'] = ev if et == 'pressure_over' else 0
        n['exit_pressure_under'] = ev if et == 'pressure_under' else 0
        n['exit_flow_over'] = ev if et == 'flow_over' else 0
        n['exit_flow_under'] = ev if et == 'flow_under' else 0
    else:
        n.update({
            'exit_if': 0, 'exit_type': '', 'exit_pressure_over': 0,
            'exit_pressure_under': 0, 'exit_flow_over': 0, 'exit_flow_under': 0,
        })
    lim = f.get('limiter')
    if lim:
        n['max_flow_or_pressure'] = float(lim.get('value', 0))
        n['max_flow_or_pressure_range'] = float(lim.get('range', 0.6))
    else:
        n['max_flow_or_pressure'] = 0
        n['max_flow_or_pressure_range'] = 0.6
    n['weight'] = float(f.get('weight', 0))
    return n


def fdiff(a, b):
    if isinstance(a, float) and isinstance(b, float):
        return abs(a - b) > 0.001
    return a != b


CORE = [
    'name', 'pump', 'pressure', 'flow', 'temperature', 'seconds',
    'volume', 'transition', 'sensor',
    'max_flow_or_pressure', 'max_flow_or_pressure_range', 'weight',
]


def analyze_profile(jframes, tframes):
    """Returns (has_major, has_exit_only, details)"""
    if len(jframes) != len(tframes):
        return True, False, [f"Frame count: Decenza={len(jframes)}, de1app={len(tframes)}"]

    major_diffs = []
    exit_only_diffs = []

    for i in range(len(jframes)):
        jf = norm_json(jframes[i])
        tf = norm_tcl(tframes[i])
        fname = jf['name'] or f'frame {i}'

        core_diffs = []
        for k in CORE:
            if fdiff(jf[k], tf[k]):
                core_diffs.append(f"    {k}: D={jf[k]}, T={tf[k]}")

        exit_if_diff = jf['exit_if'] != tf['exit_if']
        exit_detail_diffs = []
        for k in ['exit_type', 'exit_pressure_over', 'exit_pressure_under',
                   'exit_flow_over', 'exit_flow_under']:
            if fdiff(jf.get(k, 0), tf.get(k, 0)):
                exit_detail_diffs.append(f"    {k}: D={jf.get(k, 0)}, T={tf.get(k, 0)}")

        if core_diffs:
            major_diffs.append(f"  Frame {i} ({fname}): CORE DIFFS")
            major_diffs.extend(core_diffs)
            if exit_if_diff:
                major_diffs.append(f"    exit_if: D={jf['exit_if']}, T={tf['exit_if']}")
            if exit_detail_diffs:
                major_diffs.extend(exit_detail_diffs)
        elif exit_if_diff or exit_detail_diffs:
            exit_only_diffs.append(f"  Frame {i} ({fname}):")
            if exit_if_diff:
                exit_only_diffs.append(f"    exit_if: D={jf['exit_if']}, T={tf['exit_if']}")
            exit_only_diffs.extend(exit_detail_diffs)

    details = major_diffs
    if exit_only_diffs:
        details = details + (["  ---exit-only diffs---"] + exit_only_diffs if details else exit_only_diffs)
    return bool(major_diffs), bool(exit_only_diffs), details


# Build indexes
decenza = {}
for f in sorted(DECENZA_DIR.glob("*.json")):
    try:
        data = json.load(open(f))
        title = data.get('title', '')
        decenza[title.lower()] = {'file': f.name, 'data': data, 'title': title}
    except Exception:
        pass

de1app = {}
for f in sorted(DE1APP_DIR.glob("*.tcl")):
    try:
        data = parse_tcl_profile(f)
        title = data.get('title', f.stem)
        de1app[title.lower()] = {'file': f.name, 'data': data, 'title': title}
    except Exception:
        pass

cat_major = []
cat_exit_only = []
cat_perfect = []
decenza_only = []
matched = set()

for dk, dv in sorted(decenza.items()):
    mk = dk if dk in de1app else None
    if not mk:
        decenza_only.append((dv['title'], dv['file']))
        continue
    matched.add(mk)

    jd = dv['data']
    td = de1app[mk]['data']
    ptype = jd.get('legacy_profile_type', jd.get('profile_type', ''))
    is_simple = ptype in ('settings_2a', 'settings_2b')

    # For simple profiles (settings_2a/2b), de1app IGNORES stored advanced_shot frames
    # and regenerates from scalar parameters at upload time. So we only compare scalars,
    # not stored frames. For advanced profiles (settings_2c), compare stored frames.
    if is_simple:
        has_major, has_exit, details = False, False, []
    else:
        jframes = dv['data'].get('steps', [])
        tframes = de1app[mk]['data'].get('frames', [])
        has_major, has_exit, details = analyze_profile(jframes, tframes)

    # Compare profile-level settings
    # Map: (Decenza JSON key, de1app normalized key, label)
    PROFILE_SETTINGS = [
        ('target_weight', 'target_weight', 'target_weight'),
        ('target_volume', 'target_volume', 'target_volume'),
        ('espresso_temperature', 'espresso_temperature', 'espresso_temperature'),
        ('tank_desired_water_temperature', 'tank_temperature', 'tank_temperature'),
        ('maximum_pressure', 'maximum_pressure', 'maximum_pressure'),
        ('maximum_flow', 'maximum_flow', 'maximum_flow'),
        ('maximum_pressure_range_advanced', 'maximum_pressure_range', 'max_pressure_range'),
        ('maximum_flow_range_advanced', 'maximum_flow_range', 'max_flow_range'),
        ('beverage_type', 'beverage_type', 'beverage_type'),
    ]
    # Warn if simple profile has is_recipe_mode (stale conversion artifact)
    if is_simple and jd.get('is_recipe_mode', False):
        has_major = True
        details = [f"  WARNING: simple profile ({ptype}) has is_recipe_mode=true (stale recipe params will override scalars)"] + details
    if is_simple:
        # All simple profiles use these scalar parameters for frame generation.
        # De1app's pressure_to_advanced_list and flow_to_advanced_list both use
        # espresso_hold_time/espresso_decline_time (NOT flow_profile_hold_time).
        PROFILE_SETTINGS += [
            ('preinfusion_time', 'preinfusion_time', 'preinfusion_time'),
            ('preinfusion_stop_pressure', 'preinfusion_stop_pressure', 'preinfusion_stop_pressure'),
            ('preinfusion_flow_rate', 'preinfusion_flow_rate', 'preinfusion_flow_rate'),
            ('espresso_hold_time', 'espresso_hold_time', 'espresso_hold_time'),
            ('espresso_decline_time', 'espresso_decline_time', 'espresso_decline_time'),
        ]
    if ptype == 'settings_2a':
        PROFILE_SETTINGS += [
            ('espresso_pressure', 'espresso_pressure', 'espresso_pressure'),
            ('pressure_end', 'pressure_end', 'pressure_end'),
        ]
    elif ptype == 'settings_2b':
        PROFILE_SETTINGS += [
            ('flow_profile_preinfusion', 'flow_profile_preinfusion', 'flow_profile_preinfusion'),
            ('flow_profile_hold', 'flow_profile_hold', 'flow_profile_hold'),
            ('flow_profile_decline', 'flow_profile_decline', 'flow_profile_decline'),
            ('flow_profile_minimum_pressure', 'flow_profile_minimum_pressure', 'flow_min_pressure'),
        ]
    settings_diffs = []
    for jkey, tkey, label in PROFILE_SETTINGS:
        jv = jd.get(jkey, 0)
        tv = td.get(tkey, 0)
        if isinstance(jv, (int, float)) and isinstance(tv, (int, float)):
            if abs(float(jv) - float(tv)) > 0.01:
                settings_diffs.append(f"  {label}: D={jv}, T={tv}")
        elif str(jv) != str(tv):
            settings_diffs.append(f"  {label}: D={jv}, T={tv}")
    # Compare temperature stepping and presets for simple profiles
    if ptype in ('settings_2a', 'settings_2b'):
        j_temp_steps = jd.get('temp_steps_enabled', False)
        t_temp_steps = td.get('temperature_steps_enabled', 0)
        if bool(j_temp_steps) != bool(t_temp_steps):
            settings_diffs.append(f"  temp_steps_enabled: D={j_temp_steps}, T={t_temp_steps}")
        j_presets = jd.get('temperature_presets', [])
        t_presets = [td.get(f'temperature_{i}', 0) for i in range(4)]
        if j_presets and t_presets:
            for i in range(min(len(j_presets), len(t_presets))):
                if abs(float(j_presets[i]) - float(t_presets[i])) > 0.01:
                    settings_diffs.append(f"  temperature_{i}: D={j_presets[i]}, T={t_presets[i]}")
    if settings_diffs:
        has_major = True
        details = [f"  Profile-level settings:"] + settings_diffs + details

    if has_major:
        cat_major.append({'title': dv['title'], 'df': dv['file'],
                          'tf': de1app[mk]['file'], 'details': details})
    elif has_exit:
        cat_exit_only.append({'title': dv['title'], 'df': dv['file'],
                              'tf': de1app[mk]['file'], 'details': details})
    else:
        cat_perfect.append(dv['title'])

missing = [(de1app[k]['title'], de1app[k]['file']) for k in sorted(de1app) if k not in matched]

# === REPORT ===
print("=" * 80)
print("PROFILE COMPARISON: Decenza vs de1app")
print("=" * 80)

print(f"\n{'=' * 80}")
print(f"CATEGORY A: MAJOR DIFFERENCES -- core frame data differs ({len(cat_major)} profiles)")
print("These profiles have different pump/pressure/flow/temp/seconds/transition/limiter/weight values.")
print(f"{'=' * 80}")
for m in cat_major:
    print(f"\n** {m['title']} **")
    print(f"   Decenza: {m['df']}")
    print(f"   de1app:  {m['tf']}")
    for d in m['details']:
        print(d)

print(f"\n{'=' * 80}")
print(f"CATEGORY B: EXIT-CONDITION-ONLY DIFFERENCES ({len(cat_exit_only)} profiles)")
print("Only exit_if / exit_type / exit_*_over / exit_*_under differ.")
print("Core frame parameters (pump, pressure, flow, temp, seconds, etc.) are identical.")
print(f"{'=' * 80}")
for m in cat_exit_only:
    print(f"\n** {m['title']} **")
    print(f"   Decenza: {m['df']}")
    print(f"   de1app:  {m['tf']}")
    for d in m['details']:
        print(d)

print(f"\n{'=' * 80}")
print(f"CATEGORY C: PERFECT / COSMETIC-ONLY MATCHES ({len(cat_perfect)} profiles)")
print(f"{'=' * 80}")
for p in sorted(cat_perfect):
    print(f"  - {p}")

print(f"\n{'=' * 80}")
print(f"DE1APP PROFILES MISSING FROM DECENZA: {len(missing)}")
print(f"{'=' * 80}")
for t, f in sorted(missing):
    print(f"  {t} ({f})")
if not missing:
    print("  None")

print(f"\n{'=' * 80}")
print(f"DECENZA-ONLY PROFILES: {len(decenza_only)}")
print(f"{'=' * 80}")
for t, f in sorted(decenza_only):
    print(f"  {t} ({f})")

print(f"\n{'=' * 80}")
print("FINAL SUMMARY")
print(f"{'=' * 80}")
print(f"  CATEGORY A (major core diffs):       {len(cat_major)}")
print(f"  CATEGORY B (exit-condition-only):     {len(cat_exit_only)}")
print(f"  CATEGORY C (match / cosmetic):        {len(cat_perfect)}")
print(f"  Missing from Decenza:                 {len(missing)}")
print(f"  Decenza-only:                         {len(decenza_only)}")
