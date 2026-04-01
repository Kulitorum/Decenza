#!/usr/bin/env python3
"""
Test profile import by round-tripping through Decenza's C++ Profile parser.

Builds a minimal test app that loads each built-in JSON profile via
Profile::fromJson() and each de1app TCL profile via Profile::loadFromTclString(),
then prints the parsed target_weight, target_volume, frame count, and
espresso_temperature. Compares against expected values from de1app.

Usage:
    python scripts/test_profile_import.py [de1app_profiles_dir]

Requires: CMake, a C++ compiler, and Qt 6 (uses qt-cmake if available).
"""

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
DECENZA_PROFILES = PROJECT_DIR / "resources" / "profiles"

if len(sys.argv) > 1:
    DE1APP_DIR = Path(sys.argv[1])
else:
    DE1APP_DIR = Path.home() / "Documents" / "GitHub" / "de1app" / "de1plus" / "profiles"

# The test app source — uses Profile::fromJson() and fromTclString() directly
TEST_APP_SOURCE = r'''
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QDebug>
#include "profile.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        qCritical() << "Usage: test_profile_import <json_dir> <tcl_dir>";
        return 1;
    }

    QString jsonDir = argv[1];
    QString tclDir = argv[2];
    int errors = 0;
    int tested = 0;

    // Test JSON profiles
    QDir jdir(jsonDir);
    for (const auto& entry : jdir.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(entry.filePath());
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isNull()) {
            qWarning() << "SKIP (invalid JSON):" << entry.fileName();
            continue;
        }

        Profile profile = Profile::fromJson(doc);
        tested++;

        // Output: filename|title|target_weight|target_volume|espresso_temp|frame_count
        QString line = QString("%1|%2|%3|%4|%5|%6")
            .arg(entry.fileName())
            .arg(profile.title())
            .arg(profile.targetWeight(), 0, 'f', 1)
            .arg(profile.targetVolume(), 0, 'f', 1)
            .arg(profile.espressoTemperature(), 0, 'f', 1)
            .arg(profile.steps().size());
        fprintf(stdout, "JSON|%s\n", qPrintable(line));
    }

    // Test TCL profiles
    QDir tdir(tclDir);
    for (const auto& entry : tdir.entryInfoList({"*.tcl"}, QDir::Files, QDir::Name)) {
        QFile f(entry.filePath());
        if (!f.open(QIODevice::ReadOnly)) continue;

        QString content = QString::fromUtf8(f.readAll());
        Profile profile = Profile::loadFromTclString(content);
        tested++;

        QString line = QString("%1|%2|%3|%4|%5|%6")
            .arg(entry.fileName())
            .arg(profile.title())
            .arg(profile.targetWeight(), 0, 'f', 1)
            .arg(profile.targetVolume(), 0, 'f', 1)
            .arg(profile.espressoTemperature(), 0, 'f', 1)
            .arg(profile.steps().size());
        fprintf(stdout, "TCL|%s\n", qPrintable(line));
    }

    fprintf(stderr, "Tested %d profiles\n", tested);
    return errors;
}
'''

TEST_CMAKE_TEMPLATE = '''
cmake_minimum_required(VERSION 3.16)
project(test_profile_import LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Bluetooth)

# Profile sources from the main project
set(PROFILE_DIR "{project_dir}/src/profile")

add_executable(test_profile_import
    main.cpp
    ${{PROFILE_DIR}}/profile.cpp
    ${{PROFILE_DIR}}/profile.h
    ${{PROFILE_DIR}}/profileframe.cpp
    ${{PROFILE_DIR}}/profileframe.h
    ${{PROFILE_DIR}}/recipeparams.cpp
    ${{PROFILE_DIR}}/recipegenerator.cpp
    {project_dir}/src/ble/protocol/binarycodec.cpp
)

target_include_directories(test_profile_import PRIVATE
    ${{PROFILE_DIR}}
    {project_dir}/src
    {project_dir}/src/ble/protocol
)

target_link_libraries(test_profile_import PRIVATE Qt6::Core Qt6::Bluetooth)
'''


def find_qt_cmake():
    """Find qt-cmake wrapper for the platform."""
    # Try common locations
    candidates = []
    qt_base = Path.home() / "Qt"
    if qt_base.exists():
        candidates = list(qt_base.glob("*/*/bin/qt-cmake"))
        # Prefer macos
        macos = [c for c in candidates if 'macos' in str(c)]
        if macos:
            return str(macos[0])
        if candidates:
            return str(candidates[0])
    # Fallback to PATH
    try:
        result = subprocess.run(["which", "qt-cmake"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    except FileNotFoundError:
        pass
    return None


def build_test_app():
    """Build the test app and return the executable path."""
    build_dir = PROJECT_DIR / "build" / "test_profile_import"
    build_dir.mkdir(parents=True, exist_ok=True)

    # Write source files
    src_dir = build_dir / "src"
    src_dir.mkdir(exist_ok=True)
    (src_dir / "main.cpp").write_text(TEST_APP_SOURCE)
    (src_dir / "CMakeLists.txt").write_text(
        TEST_CMAKE_TEMPLATE.format(project_dir=str(PROJECT_DIR))
    )

    # Find qt-cmake
    qt_cmake = find_qt_cmake()
    if not qt_cmake:
        print("Error: qt-cmake not found. Set Qt path or install Qt.")
        sys.exit(1)

    # Find ninja and cmake
    ninja_paths = list(Path.home().glob("Qt/Tools/Ninja/ninja"))
    cmake_paths = list(Path.home().glob("Qt/Tools/CMake/CMake.app/Contents/bin/cmake"))
    cmake_extra = []
    if ninja_paths:
        cmake_extra = ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja_paths[0]}"]

    # Ensure cmake is in PATH for qt-cmake wrapper
    env = os.environ.copy()
    if cmake_paths:
        env["PATH"] = str(cmake_paths[0].parent) + ":" + env.get("PATH", "")

    # Configure
    print(f"Configuring with {qt_cmake}...")
    result = subprocess.run(
        [qt_cmake, str(src_dir)] + cmake_extra,
        cwd=str(build_dir), capture_output=True, text=True, env=env
    )
    if result.returncode != 0:
        print(f"CMake configure failed:\n{result.stderr}")
        sys.exit(1)

    # Build
    print("Building...")
    cmake_bin = str(cmake_paths[0]) if cmake_paths else "cmake"
    result = subprocess.run(
        [cmake_bin, "--build", "."],
        cwd=str(build_dir), capture_output=True, text=True, env=env
    )
    if result.returncode != 0:
        print(f"Build failed:\n{result.stdout}\n{result.stderr}")
        sys.exit(1)

    # Find executable
    exe = build_dir / "test_profile_import"
    if not exe.exists():
        # Try macOS app bundle
        exe = build_dir / "test_profile_import.app" / "Contents" / "MacOS" / "test_profile_import"
    if not exe.exists():
        print(f"Executable not found in {build_dir}")
        sys.exit(1)

    return str(exe)


def parse_de1app_values(de1app_dir):
    """Extract expected values from de1app TCL profiles."""
    expected = {}
    for f in Path(de1app_dir).glob("*.tcl"):
        with open(f, encoding='utf-8', errors='replace') as fh:
            content = fh.read()
        m = re.search(r'profile_title\s+\{([^}]*)\}', content)
        if not m:
            m = re.search(r'profile_title\s+(\S+)', content)
        title = m.group(1).strip() if m else f.stem

        # Use type-dependent keys: simple profiles use non-advanced, advanced use _advanced
        m = re.search(r'settings_profile_type\s+(\S+)', content)
        ptype = m.group(1) if m else ''
        is_advanced = ptype.startswith('settings_2c')

        if is_advanced:
            m = re.search(r'final_desired_shot_weight_advanced\s+([0-9.]+)', content)
        else:
            m = re.search(r'final_desired_shot_weight(?!_)\s+([0-9.]+)', content)
        tw = float(m.group(1)) if m else 0.0

        if is_advanced:
            m = re.search(r'final_desired_shot_volume_advanced\s+([0-9.]+)', content)
        else:
            m = re.search(r'final_desired_shot_volume(?!_)\s+([0-9.]+)', content)
        tv = float(m.group(1)) if m else 0.0

        expected[title.lower()] = {'file': f.name, 'weight': tw, 'volume': tv}
    return expected


def main():
    print("=" * 70)
    print("Profile Import Test")
    print("=" * 70)

    # Build test app
    exe = build_test_app()
    print(f"Built: {exe}\n")

    # Run test app
    tcl_dir = str(DE1APP_DIR) if DE1APP_DIR.exists() else str(DECENZA_PROFILES)
    result = subprocess.run(
        [exe, str(DECENZA_PROFILES), tcl_dir],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Test app failed:\n{result.stderr}")
        sys.exit(1)

    print(result.stderr.strip())

    # Parse de1app expected values
    de1app_expected = {}
    if DE1APP_DIR.exists():
        de1app_expected = parse_de1app_values(DE1APP_DIR)

    # Parse test output and compare
    json_results = {}
    tcl_results = {}
    errors = []

    for line in result.stdout.strip().split('\n'):
        if not line:
            continue
        parts = line.split('|')
        if len(parts) != 7:
            continue
        source, filename, title, tw, tv, temp, frames = parts
        entry = {
            'file': filename, 'title': title,
            'weight': float(tw), 'volume': float(tv),
            'temperature': float(temp), 'frames': int(frames)
        }
        if source == 'JSON':
            json_results[title.lower()] = entry
        else:
            tcl_results[title.lower()] = entry

    # Compare JSON-loaded profiles against de1app expected values
    print(f"\n{'=' * 70}")
    print("JSON Profile Import Verification")
    print(f"{'=' * 70}")

    mismatches = []
    for title_lower, jdata in sorted(json_results.items()):
        if title_lower not in de1app_expected:
            continue
        expected = de1app_expected[title_lower]
        diffs = []
        if abs(jdata['weight'] - expected['weight']) > 0.1:
            diffs.append(f"weight: got {jdata['weight']}, expected {expected['weight']}")
        if abs(jdata['volume'] - expected['volume']) > 0.1:
            diffs.append(f"volume: got {jdata['volume']}, expected {expected['volume']}")
        if diffs:
            mismatches.append((jdata['title'], jdata['file'], diffs))

    if mismatches:
        print(f"\nFAILED: {len(mismatches)} profiles have wrong values after fromJson():")
        for title, fname, diffs in mismatches:
            print(f"  {title} ({fname})")
            for d in diffs:
                print(f"    {d}")
    else:
        matched = sum(1 for t in json_results if t in de1app_expected)
        print(f"\nPASSED: All {matched} matched JSON profiles have correct weight/volume values")

    # Compare TCL-loaded profiles against expected values
    if tcl_results:
        print(f"\n{'=' * 70}")
        print("TCL Profile Import Verification")
        print(f"{'=' * 70}")

        tcl_mismatches = []
        for title_lower, tdata in sorted(tcl_results.items()):
            if title_lower not in de1app_expected:
                continue
            expected = de1app_expected[title_lower]
            diffs = []
            if abs(tdata['weight'] - expected['weight']) > 0.1:
                diffs.append(f"weight: got {tdata['weight']}, expected {expected['weight']}")
            if abs(tdata['volume'] - expected['volume']) > 0.1:
                diffs.append(f"volume: got {tdata['volume']}, expected {expected['volume']}")
            if diffs:
                tcl_mismatches.append((tdata['title'], tdata['file'], diffs))

        if tcl_mismatches:
            print(f"\nFAILED: {len(tcl_mismatches)} profiles have wrong values after fromTclString():")
            for title, fname, diffs in tcl_mismatches:
                print(f"  {title} ({fname})")
                for d in diffs:
                    print(f"    {d}")
        else:
            matched = sum(1 for t in tcl_results if t in de1app_expected)
            print(f"\nPASSED: All {matched} matched TCL profiles have correct weight/volume values")

    # Check for volume=36 or volume=100 in any loaded profile (likely bugs)
    print(f"\n{'=' * 70}")
    print("Suspicious Volume Values Check")
    print(f"{'=' * 70}")
    suspicious = []
    for title, data in sorted({**json_results, **tcl_results}.items()):
        if data['volume'] in (36.0, 100.0):
            # Check if de1app actually has this value
            if title in de1app_expected and abs(de1app_expected[title]['volume'] - data['volume']) < 0.1:
                continue  # Legitimate
            suspicious.append((data['title'], data['file'], data['volume']))

    if suspicious:
        print(f"\nWARNING: {len(suspicious)} profiles have suspicious volume values:")
        for title, fname, vol in suspicious:
            print(f"  {title} ({fname}): volume={vol}")
    else:
        print("\nPASSED: No suspicious volume values (36 or 100) found")

    print(f"\n{'=' * 70}")
    print(f"Total: {len(json_results)} JSON + {len(tcl_results)} TCL profiles tested")
    print(f"{'=' * 70}")


if __name__ == '__main__':
    main()
