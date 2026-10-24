#!/usr/bin/env python3
"""Incremental Wasm dependency for a native Qt Creator build."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import package_webclient


EMBEDDED_FILES = (*package_webclient.BASE_FILES, package_webclient.MANIFEST_NAME,
                  *(name + ".gz" for name in package_webclient.BASE_FILES
                    if name.endswith((".js", ".wasm"))))


def read_cache(path: Path) -> dict[str, str]:
    result = {}
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith(("#", "//")) or ":" not in line or "=" not in line:
                continue
            key, value = line.split("=", 1)
            result[key.split(":", 1)[0]] = value
    return result


def qt_version(prefix: Path) -> str:
    version_file = prefix / "lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake"
    if version_file.is_file():
        match = re.search(r'set\(PACKAGE_VERSION "([0-9.]+)"\)', version_file.read_text(encoding="utf-8"))
        if match:
            return match.group(1)
    return ""


def discover(arguments: argparse.Namespace) -> dict[str, str]:
    source = arguments.source_dir.resolve()
    build = arguments.build_dir
    if not build and arguments.bundle_dir:
        candidate = arguments.bundle_dir.resolve().parents[2]
        if (candidate / "CMakeCache.txt").is_file():
            build = candidate
    if not build:
        candidates = []
        for cache_path in (source / "build").glob("*/CMakeCache.txt"):
            cache = read_cache(cache_path)
            if (cache.get("EMSCRIPTEN") == "1" and cache.get("Qt6_DIR")
                    and cache.get("CMAKE_HOME_DIRECTORY") == str(source)
                    and qt_version(Path(cache["Qt6_DIR"]).parents[2]) == "6.12.0"):
                candidates.append((cache.get("CMAKE_BUILD_TYPE") != "Release", str(cache_path.parent)))
        if candidates:
            build = Path(sorted(candidates)[0][1])
    cache = read_cache(build / "CMakeCache.txt") if build else {}
    native_qt = arguments.native_qt_dir.resolve().parents[2]
    wasm_qt = arguments.wasm_qt
    if not wasm_qt and cache.get("Qt6_DIR"):
        wasm_qt = Path(cache["Qt6_DIR"]).parents[2]
    wasm_qt = wasm_qt or native_qt.parent / "wasm_singlethread"
    if not (wasm_qt / "lib/cmake/Qt6/qt.toolchain.cmake").is_file():
        raise ValueError("Install Qt 6.12.0 wasm_singlethread, or set DECENZA_WASM_QT_DIR")
    if qt_version(wasm_qt) != "6.12.0":
        raise ValueError("DECENZA_WASM_QT_DIR must select Qt 6.12.0")
    host_qt = arguments.host_qt or Path(cache.get("QT_HOST_PATH", str(native_qt)))
    if not (host_qt / "lib/cmake/Qt6/Qt6Config.cmake").is_file():
        raise ValueError("Set DECENZA_WASM_HOST_QT_DIR to matching native Qt 6.12.0")
    if qt_version(host_qt) != "6.12.0":
        raise ValueError("DECENZA_WASM_HOST_QT_DIR must select native Qt 6.12.0")
    emsdk = arguments.emsdk or (Path(os.environ["EMSDK"]) if os.environ.get("EMSDK") else None)
    if not emsdk and cache.get("CMAKE_CXX_COMPILER"):
        emsdk = Path(cache["CMAKE_CXX_COMPILER"]).parents[2]
    if not emsdk or not (emsdk / ".emscripten").is_file():
        raise ValueError("Install/activate Emscripten 5.0.5 and set EMSDK or DECENZA_WASM_EMSDK_DIR")
    ninja_candidates = [cache.get("CMAKE_MAKE_PROGRAM", ""), shutil.which("ninja") or "",
                        str(native_qt.parent.parent / "Tools/Ninja" / ("ninja.exe" if os.name == "nt" else "ninja"))]
    ninja = next((candidate for candidate in ninja_candidates if candidate and Path(candidate).is_file()), "")
    if not ninja:
        raise ValueError("Install Ninja (Qt Tools/Ninja or PATH) for the Wasm dependency")
    build = (build or arguments.native_build_dir / "wasm").resolve()
    if cache and cache.get("CMAKE_HOME_DIRECTORY") != str(source):
        raise ValueError("DECENZA_WASM_BUILD_DIR belongs to another source checkout")
    bundle = build / "src/webclient/deployment"
    if arguments.bundle_dir and arguments.bundle_dir.resolve() != bundle:
        raise ValueError("LOCAL bundle directory must be <Wasm build>/src/webclient/deployment; use PREBUILT for an external artifact")
    return {
        "source": str(source), "build": str(build), "bundle": str(bundle),
        "cmake": arguments.cmake, "ninja": ninja,
        "qt_wasm": str(wasm_qt.resolve()), "qt_host": str(host_qt.resolve()),
        "emsdk": str(emsdk.resolve()),
        "qtro": cache.get("DECENZA_QTRO_PREFIX", str(source / "build/qtro-wasm-install")),
        "multimedia": cache.get("DECENZA_QTMULTIMEDIA_PREFIX", str(source / "build/qtmultimedia-wasm-install")),
    }


def child_environment(config: dict[str, str]) -> dict[str, str]:
    environment = os.environ.copy()
    # Qt's generated toolchain serializes its settings into these environment
    # variables (Qt6/qt.toolchain.cmake:15-20). Do not inherit the native kit's
    # serialized toolchain into the independent Wasm configure.
    for key in list(environment):
        if key.startswith("_QT_TOOLCHAIN_") or key in {
                "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS", "QTDIR", "QT_ROOT_DIR",
                "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "CMAKE_PREFIX_PATH"}:
            environment.pop(key)
    sdk = Path(config["emsdk"])
    environment["EMSDK"] = str(sdk)
    environment["EM_CONFIG"] = str(sdk / ".emscripten")
    paths = [sdk, sdk / "upstream/emscripten", sdk / "upstream/bin",
             Path(config["ninja"]).parent, Path(config["cmake"]).parent]
    for tool in ("node", "python"):
        for directory in sorted((sdk / tool).glob("*"), reverse=True):
            if directory.is_dir():
                paths.extend((directory / "bin", directory))
    environment["PATH"] = os.pathsep.join(map(str, paths)) + os.pathsep + environment.get("PATH", "")
    return environment


def run_command(command: list[str], environment: dict[str, str]) -> None:
    print("[webclient] " + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, env=environment, check=True)


def configure_command(config: dict[str, str], source: Path, build: Path) -> list[str]:
    return [config["cmake"], "-S", str(source), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_MAKE_PROGRAM=" + config["ninja"],
            "-DCMAKE_TOOLCHAIN_FILE=" + str(Path(config["qt_wasm"]) / "lib/cmake/Qt6/qt.toolchain.cmake"),
            "-DQT_HOST_PATH=" + config["qt_host"]]


def ensure_modules(config: dict[str, str], environment: dict[str, str], runner) -> None:
    source = Path(config["source"])
    qt_sources = Path(config["qt_wasm"]).parent / "Src"
    for name, prefix_key, module in (("qtmultimedia", "multimedia", "Multimedia"),
                                     ("qtremoteobjects", "qtro", "RemoteObjects")):
        prefix = Path(config[prefix_key])
        if (prefix / f"lib/cmake/Qt6{module}/Qt6{module}Config.cmake").is_file():
            continue
        module_source = qt_sources / name
        version_file = module_source / ".cmake.conf"
        if not version_file.is_file() or 'set(QT_REPO_MODULE_VERSION "6.12.0")' not in version_file.read_text(encoding="utf-8"):
            raise ValueError(f"Install stock Qt 6.12.0 Sources/{name}; missing Wasm module at {prefix}")
        module_build = Path(config["build"]) / "qt-dependencies" / name
        command = configure_command(config, module_source, module_build)
        if name == "qtmultimedia":
            command = configure_command(config, source / "tools/qtmodules-wasm", module_build)
            command.extend(["-DQt6_DIR=" + str(Path(config["qt_wasm"]) / "lib/cmake/Qt6"),
                            "-DQT_MULTIMEDIA_SOURCE_DIR=" + str(module_source),
                            "-DQT_MULTIMEDIA_INSTALL_PREFIX=" + str(prefix)])
            runner(command, environment)
            runner([config["cmake"], "--build", str(module_build), "--target", "qtmultimedia", "--parallel", "2"], environment)
        else:
            command.extend(["-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                            "-DCMAKE_STAGING_PREFIX=" + str(prefix), "-DQT_BUILD_TESTS=OFF",
                            "-DQT_BUILD_EXAMPLES=OFF", "-DQT_BUILD_TOOLS_BY_DEFAULT=OFF", "-DQT_BUILD_DOCS=OFF"])
            runner(command, environment)
            runner([config["cmake"], "--build", str(module_build), "--parallel", "2"], environment)
            runner([config["cmake"], "--install", str(module_build)], environment)


def build_and_stage(config: dict[str, str], expected_package_id: str, output: Path,
                    runner=run_command) -> None:
    environment = child_environment(config)
    ensure_modules(config, environment, runner)
    command = configure_command(config, Path(config["source"]), Path(config["build"]))
    command.extend(["-DDECENZA_QTRO_PREFIX=" + config["qtro"],
                    "-DDECENZA_QTMULTIMEDIA_PREFIX=" + config["multimedia"]])
    cache = read_cache(Path(config["build"]) / "CMakeCache.txt")
    options = dict(argument[2:].split("=", 1) for argument in command
                   if argument.startswith("-D"))
    if (cache.get("CMAKE_HOME_DIRECTORY") != config["source"]
            or any(cache.get(key) != value for key, value in options.items())):
        runner(command, environment)
    # Ninja already reruns CMake for changed inputs. An explicit configure on
    # every source edit adds a second configure to that normal dependency path.
    # Use the normal parallel budget (or CMAKE_BUILD_PARALLEL_LEVEL), rather
    # than serializing the Wasm half of a Qt Creator build onto two workers.
    runner([config["cmake"], "--build", config["build"], "--parallel"], environment)
    print("[webclient] Verifying the newly built bundle against the native package identity", flush=True)
    package_webclient.install_bundle(Path(config["bundle"]), expected_package_id, output)
    # Qt's resource source list is fixed at native configure time. Empty gzip
    # slots exist only in this private embedding staging directory; the strict
    # verified deployment and its manifest remain unchanged. HTTP only selects
    # a gzip slot when the verified manifest advertises its size and digest.
    for filename in EMBEDDED_FILES:
        path = output / filename
        if not path.exists():
            path.write_bytes(b"")
    print("[webclient] Matching Wasm bundle ready for native resource compilation", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    discover_parser = commands.add_parser("discover")
    for name in ("source-dir", "native-build-dir", "native-qt-dir"):
        discover_parser.add_argument("--" + name, type=Path, required=True)
    for name in ("build-dir", "bundle-dir", "wasm-qt", "host-qt", "emsdk"):
        discover_parser.add_argument("--" + name, type=lambda value: Path(value) if value else None)
    discover_parser.add_argument("--cmake", required=True)
    discover_parser.add_argument("--output", type=Path, required=True)
    build_parser = commands.add_parser("build")
    build_parser.add_argument("--config", type=Path, required=True)
    build_parser.add_argument("--expected-package-id", required=True)
    build_parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "discover":
            configuration = discover(args)
            encoded = json.dumps(configuration, sort_keys=True, indent=2) + "\n"
            if not args.output.is_file() or args.output.read_text(encoding="utf-8") != encoded:
                args.output.write_text(encoded, encoding="utf-8")
        else:
            build_and_stage(json.loads(args.config.read_text(encoding="utf-8")),
                            args.expected_package_id, args.output_dir)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"webclient build error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
