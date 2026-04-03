#!/usr/bin/env python3
"""
Build / flash / clean script for the Nucleo Zephyr targets in this repo.

Sets all required environment variables before invoking west so the
toolchain is always found regardless of how the script is launched
(terminal, VS Code task, cron, etc.).

Usage:
    python3 build.py
    python3 build.py build
    python3 build.py build g4
    python3 build.py build f4
    python3 build.py clean f4
    python3 build.py flash g4
    python3 build.py flash f4 jlink
    python3 build.py build h5
    python3 build.py menuconfig g4
    python3 build.py flash h5
    python3 build.py debug g4 openocd
"""

import argparse
from datetime import datetime
import glob
import os
import sys
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

# ══════════════════════════════════════════════════════════════════════
#  USER CONFIGURATION — update these paths to match your system
# ══════════════════════════════════════════════════════════════════════
# Absolute path to your Zephyr checkout (the directory containing
# kernel/, include/, boards/, etc.).  If you used `west init` the
# path is typically  <workspace>/zephyr.
ZEPHYR_BASE_OVERRIDE = None          # e.g. Path("/home/you/zephyrproject/zephyr")

# (Optional) Path to a locally-built ST-patched OpenOCD tree.
# Only needed if you flash/debug the H7 or H5 targets with the local
# OpenOCD build.  Leave as None to use the system OpenOCD.
EXTERNAL_DIR_OVERRIDE = None         # e.g. Path("/home/you/external")
# ══════════════════════════════════════════════════════════════════════

# ── Paths ────────────────────────────────────────────────────────────
PROJECT_DIR  = Path(__file__).resolve().parent

_zephyr_env = os.environ.get("ZEPHYR_BASE")
if ZEPHYR_BASE_OVERRIDE is not None:
    ZEPHYR_BASE = Path(ZEPHYR_BASE_OVERRIDE)
elif _zephyr_env:
    ZEPHYR_BASE = Path(_zephyr_env)
else:
    # Fallback: look for a 'zephyr' directory next to this project,
    # or one level up, or two levels up.
    for _candidate in (
        PROJECT_DIR / "zephyr",
        PROJECT_DIR.parent / "zephyr",
        PROJECT_DIR.parent.parent / "zephyr",
        PROJECT_DIR.parent.parent.parent / "zephyr",
    ):
        if (_candidate / "west.yml").exists():
            ZEPHYR_BASE = _candidate
            break
    else:
        print("ERROR: Cannot locate Zephyr.  Set ZEPHYR_BASE_OVERRIDE in build.py,", file=sys.stderr)
        print("       or set the ZEPHYR_BASE environment variable.", file=sys.stderr)
        sys.exit(1)

if EXTERNAL_DIR_OVERRIDE is not None:
    EXTERNAL_DIR = Path(EXTERNAL_DIR_OVERRIDE)
else:
    # Best-effort: look next to the Zephyr base
    EXTERNAL_DIR = ZEPHYR_BASE.parent / "external"

DEFAULT_BUILD_DIR = PROJECT_DIR / "build"

IS_WINDOWS = os.name == "nt"


@dataclass(frozen=True)
class HostTools:
    toolchain_path: Path
    openocd_bin: str
    openocd_scripts: Path
    jlink_commander: str


def _existing_path_from_candidates(*candidates: str) -> Path | None:
    for candidate in candidates:
        expanded = os.path.expandvars(os.path.expanduser(candidate))
        matches = sorted(glob.glob(expanded), reverse=True)
        for match in matches:
            path = Path(match)
            if path.exists():
                return path
    return None


def _resolve_cubeclt_root() -> Path | None:
    env_root = os.environ.get("STM32CUBECLT_ROOT")
    if env_root:
        root = Path(os.path.expandvars(os.path.expanduser(env_root)))
        if root.exists():
            return root

    if IS_WINDOWS:
        return _existing_path_from_candidates(
            "C:/ST/STM32CubeCLT_*",
            "C:/Program Files/STMicroelectronics/STM32CubeCLT_*",
        )

    return _existing_path_from_candidates(
        "/home/*/st/stm32cubeclt_*",
        "/opt/st/stm32cubeclt_*",
        "/usr/local/st/stm32cubeclt_*",
    )


def _resolve_toolchain_path(cubeclt_root: Path | None) -> Path | None:
    env_toolchain = os.environ.get("GNUARMEMB_TOOLCHAIN_PATH")
    if env_toolchain:
        path = Path(os.path.expandvars(os.path.expanduser(env_toolchain)))
        if path.exists():
            return path

    if cubeclt_root is not None:
        candidate = cubeclt_root / "GNU-tools-for-STM32"
        if candidate.exists():
            return candidate

    if IS_WINDOWS:
        return _existing_path_from_candidates(
            "C:/ST/STM32CubeCLT_*/GNU-tools-for-STM32",
            "C:/Program Files/STMicroelectronics/STM32CubeCLT_*/GNU-tools-for-STM32",
        )

    return _existing_path_from_candidates(
        "/home/*/st/stm32cubeclt_*/GNU-tools-for-STM32",
        "/opt/st/stm32cubeclt_*/GNU-tools-for-STM32",
        "/usr/local/st/stm32cubeclt_*/GNU-tools-for-STM32",
        "/home/eddie/st/stm32cubeclt_*/GNU-tools-for-STM32",
    )


def _resolve_openocd_bin(cubeclt_root: Path | None) -> str:
    env_bin = os.environ.get("OPENOCD_BIN")
    if env_bin:
        return env_bin

    executable_names = ["openocd.exe", "openocd"] if IS_WINDOWS else ["openocd"]
    for name in executable_names:
        detected = shutil.which(name)
        if detected:
            return detected

    if cubeclt_root is not None:
        exe_name = "openocd.exe" if IS_WINDOWS else "openocd"
        candidate = cubeclt_root / "OpenOCD" / "bin" / exe_name
        if candidate.exists():
            return str(candidate)

    fallback = _existing_path_from_candidates(
        "C:/ST/STM32CubeCLT_*/OpenOCD/bin/openocd.exe",
        "C:/Program Files/STMicroelectronics/STM32CubeCLT_*/OpenOCD/bin/openocd.exe",
        "/usr/local/bin/openocd",
        "/opt/homebrew/bin/openocd",
        "/usr/bin/openocd",
    )
    if fallback is not None:
        return str(fallback)

    return "openocd.exe" if IS_WINDOWS else "openocd"


def _resolve_openocd_scripts(cubeclt_root: Path | None) -> Path:
    env_scripts = os.environ.get("OPENOCD_SCRIPTS")
    if env_scripts:
        path = Path(os.path.expandvars(os.path.expanduser(env_scripts)))
        if path.exists():
            return path

    if cubeclt_root is not None:
        candidate = cubeclt_root / "OpenOCD" / "share" / "openocd" / "scripts"
        if candidate.exists():
            return candidate

    fallback = _existing_path_from_candidates(
        "C:/ST/STM32CubeCLT_*/OpenOCD/share/openocd/scripts",
        "C:/Program Files/STMicroelectronics/STM32CubeCLT_*/OpenOCD/share/openocd/scripts",
        "/usr/local/share/openocd/scripts",
        "/opt/homebrew/share/openocd/scripts",
        "/usr/share/openocd/scripts",
    )
    if fallback is not None:
        return fallback

    return Path("C:/ST/STM32CubeCLT/OpenOCD/share/openocd/scripts") if IS_WINDOWS else Path("/usr/local/share/openocd/scripts")


def _resolve_jlink_commander() -> str:
    env_commander = os.environ.get("JLINK_COMMANDER")
    if env_commander:
        return env_commander

    candidates = ["JLink.exe", "JLinkExe"] if IS_WINDOWS else ["JLinkExe", "JLink.exe"]
    for name in candidates:
        detected = shutil.which(name)
        if detected:
            return detected

    fallback = _existing_path_from_candidates(
        "C:/Program Files/SEGGER/JLink/JLink.exe",
        "C:/Program Files (x86)/SEGGER/JLink/JLink.exe",
        "/usr/bin/JLinkExe",
        "/usr/local/bin/JLinkExe",
    )
    if fallback is not None:
        return str(fallback)

    return "JLink.exe" if IS_WINDOWS else "JLinkExe"


def resolve_host_tools() -> HostTools:
    cubeclt_root = _resolve_cubeclt_root()
    toolchain_path = _resolve_toolchain_path(cubeclt_root)
    if toolchain_path is None:
        print("ERROR: GNU Arm Embedded toolchain not found.", file=sys.stderr)
        print("  Set GNUARMEMB_TOOLCHAIN_PATH or STM32CUBECLT_ROOT to your STM32CubeCLT install.", file=sys.stderr)
        sys.exit(1)

    return HostTools(
        toolchain_path=toolchain_path,
        openocd_bin=_resolve_openocd_bin(cubeclt_root),
        openocd_scripts=_resolve_openocd_scripts(cubeclt_root),
        jlink_commander=_resolve_jlink_commander(),
    )


@dataclass(frozen=True)
class BoardConfig:
    alias: str
    board: str
    build_dir: Path
    jlink_device: str
    default_flash_runner: str
    openocd_config: Path | None = None
    openocd_bin: str | None = None
    openocd_scripts: Path | None = None


# Locally-built ST OpenOCD (needed for H7 — system OpenOCD has incompatible scripts).
_LOCAL_ST_OPENOCD_BIN = EXTERNAL_DIR / "OpenOCD" / "src" / "openocd"
_LOCAL_ST_OPENOCD_TCL = EXTERNAL_DIR / "OpenOCD" / "tcl"

BOARD_CONFIGS = {
    "g4": BoardConfig(
        alias="g4",
        board="nucleo_g474re",
        build_dir=DEFAULT_BUILD_DIR,
        jlink_device="STM32G474RE",
        default_flash_runner="openocd",
        openocd_config=PROJECT_DIR / "a" / "nucleo_g474re.cfg",
    ),
    "f4": BoardConfig(
        alias="f4",
        board="nucleo_f446re",
        build_dir=PROJECT_DIR / "build-f4",
        jlink_device="STM32F446RE",
        default_flash_runner="jlink",
    ),
    "h7": BoardConfig(
        alias="h7",
        board="stm32h750b_dk",
        build_dir=PROJECT_DIR / "build-h7",
        jlink_device="STM32H735IG",
        default_flash_runner="openocd",
        openocd_config=ZEPHYR_BASE / "boards" / "st" / "stm32h750b_dk" / "support" / "openocd.cfg",
        openocd_bin=str(_LOCAL_ST_OPENOCD_BIN) if _LOCAL_ST_OPENOCD_BIN.exists() else None,
        openocd_scripts=_LOCAL_ST_OPENOCD_TCL if _LOCAL_ST_OPENOCD_TCL.exists() else None,
    ),
    "h5": BoardConfig(
        alias="h5",
        board="nucleo_h503rb",
        build_dir=PROJECT_DIR / "build-h5",
        jlink_device="STM32H503RB",
        default_flash_runner="openocd",
        openocd_config=ZEPHYR_BASE / "boards" / "st" / "nucleo_h503rb" / "support" / "openocd.cfg",
        openocd_bin=str(_LOCAL_ST_OPENOCD_BIN) if _LOCAL_ST_OPENOCD_BIN.exists() else None,
        openocd_scripts=_LOCAL_ST_OPENOCD_TCL if _LOCAL_ST_OPENOCD_TCL.exists() else None,
    ),
}

BOARD_ALIASES = {
    "g4": "g4",
    "g474": "g4",
    "nucleo_g474re": "g4",
    "f4": "f4",
    "f446": "f4",
    "nucleo_f446re": "f4",
    "h7": "h7",
    "h750": "h7",
    "stm32h750b_dk": "h7",
    "h5": "h5",
    "h503": "h5",
    "nucleo_h503rb": "h5",
}

RUNNER_ALIASES = {
    "jlink": "jlink",
    "jl": "jlink",
    "stlink": "openocd",
    "st-link": "openocd",
    "st": "openocd",
    "openocd": "openocd",
    "ocd": "openocd",
}

DEFAULT_DEBUG_RUNNER = "openocd"

# ── Environment ──────────────────────────────────────────────────────
def setup_env():
    """Return a copy of os.environ with every Zephyr variable set."""
    env = os.environ.copy()
    host_tools = resolve_host_tools()

    env["ZEPHYR_BASE"]              = str(ZEPHYR_BASE)
    env["ZEPHYR_TOOLCHAIN_VARIANT"] = "gnuarmemb"
    env["GNUARMEMB_TOOLCHAIN_PATH"] = str(host_tools.toolchain_path)

    # Prevent CMake from finding the incompatible Zephyr SDK 1.0.0
    env.pop("ZEPHYR_SDK_INSTALL_DIR", None)

    # Make sure west is on PATH
    home = Path.home()
    local_bin = str(home / ".local" / "bin")
    if local_bin not in env.get("PATH", ""):
        env["PATH"] = local_bin + os.pathsep + env.get("PATH", "")

    return env, host_tools


def find_west(env):
    """Return the absolute path to west, or exit with a clear message."""
    west = shutil.which("west", path=env.get("PATH"))
    if west is None:
        print("ERROR: 'west' not found on PATH.", file=sys.stderr)
        print("  Install with:  pip3 install --user west", file=sys.stderr)
        sys.exit(1)
    return west


def resolve_board(name: str | None) -> BoardConfig:
    key = BOARD_ALIASES.get((name or "g4").lower())
    if key is None:
        choices = ", ".join(sorted(BOARD_ALIASES))
        raise ValueError(f"Unknown board '{name}'. Use one of: {choices}")
    return BOARD_CONFIGS[key]


def resolve_runner(name: str | None, *, default: str) -> str:
    key = RUNNER_ALIASES.get((name or default).lower())
    if key is None:
        choices = ", ".join(sorted(RUNNER_ALIASES))
        raise ValueError(f"Unknown runner '{name}'. Use one of: {choices}")
    return key


def west_build_args(board_cfg: BoardConfig, west: str, pristine: bool, host_tools: HostTools) -> list[str]:
    args = [
        west, "build",
        "-p", "always" if pristine else "auto",
        "-b", board_cfg.board,
        "-d", str(board_cfg.build_dir),
        str(PROJECT_DIR),
        "--",
        "-DZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb",
        f"-DGNUARMEMB_TOOLCHAIN_PATH={host_tools.toolchain_path}",
    ]
    if project_dir_looks_like_build_dir():
        args.insert(2, "--force")
    return args


def project_dir_looks_like_build_dir() -> bool:
    return (PROJECT_DIR / "CMakeCache.txt").exists() or (PROJECT_DIR / "CMakeFiles").exists()


def generated_root_kconfig_dir() -> Path | None:
    candidate = PROJECT_DIR / "Kconfig"
    if not candidate.is_dir():
        return None

    generated_markers = ("Kconfig.modules", "Kconfig.sysbuild.modules", "Kconfig.dts")
    if any((candidate / marker).exists() for marker in generated_markers):
        return candidate

    return None


def quarantine_root_build_artifacts() -> list[str]:
    artifact_paths: list[Path] = []

    for name in ("CMakeCache.txt", "CMakeFiles", "sysbuild_modules.txt", "zephyr_modules.txt", "zephyr_settings.txt"):
        candidate = PROJECT_DIR / name
        if candidate.exists():
            artifact_paths.append(candidate)

    generated_kconfig = generated_root_kconfig_dir()
    if generated_kconfig is not None:
        artifact_paths.append(generated_kconfig)

    if not artifact_paths:
        return []

    backup_dir = PROJECT_DIR / ".root-artifact-backup" / datetime.now().strftime("auto-%Y%m%d-%H%M%S-%f")
    backup_dir.mkdir(parents=True, exist_ok=True)

    moved_names: list[str] = []
    for artifact_path in artifact_paths:
        shutil.move(str(artifact_path), str(backup_dir / artifact_path.name))
        moved_names.append(artifact_path.name)

    return moved_names


def build_dir_needs_reset(build_dir: Path) -> bool:
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.exists():
        return False

    try:
        cache_text = cache_file.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False

    normalized_project_dir = PROJECT_DIR.as_posix()
    normalized_zephyr_base = ZEPHYR_BASE.as_posix()
    expected_markers = (
        f"APPLICATION_SOURCE_DIR:PATH={normalized_project_dir}",
        f"ZEPHYR_BASE:PATH={normalized_zephyr_base}",
    )
    return any(marker not in cache_text for marker in expected_markers)


def reset_build_dir_if_needed(board_cfg: BoardConfig) -> bool:
    if not build_dir_needs_reset(board_cfg.build_dir):
        return False

    shutil.rmtree(board_cfg.build_dir, ignore_errors=False)
    return True


def west_runner_args(board_cfg: BoardConfig, runner: str, host_tools: HostTools) -> list[str]:
    if runner == "openocd":
        ocd_bin = board_cfg.openocd_bin or host_tools.openocd_bin
        ocd_scripts = board_cfg.openocd_scripts or host_tools.openocd_scripts
        args = [
            "--runner", "openocd",
            "--openocd", ocd_bin,
            "--openocd-search", str(ocd_scripts),
        ]
        if board_cfg.openocd_config is not None:
            args.extend(["--config", str(board_cfg.openocd_config)])
        return args

    if runner == "jlink":
        return [
            "--runner", "jlink",
            "--",
            "--device", board_cfg.jlink_device,
            "--commander", host_tools.jlink_commander,
            "--tool-opt=-autoconnect 1",
        ]

    raise ValueError(f"Unsupported runner '{runner}'")


# ── Commands ─────────────────────────────────────────────────────────
def cmd_build(board_cfg: BoardConfig, pristine: bool = False):
    env, host_tools = setup_env()
    west = find_west(env)

    moved_root_artifacts = quarantine_root_build_artifacts()
    build_dir_reset = reset_build_dir_if_needed(board_cfg)

    args = west_build_args(board_cfg, west, pristine, host_tools)

    print(f"{'[CLEAN] ' if pristine else ''}Building {board_cfg.board} ({board_cfg.alias}) …")
    print(f"  west     = {west}")
    print(f"  build dir   = {board_cfg.build_dir}")
    print(f"  ZEPHYR_BASE = {env['ZEPHYR_BASE']}")
    print(f"  toolchain   = gnuarmemb ({host_tools.toolchain_path})")
    if moved_root_artifacts:
        print(f"  note        = quarantined generated root artifacts: {', '.join(moved_root_artifacts)}")
    elif project_dir_looks_like_build_dir():
        print("  note        = root contains CMake build markers, forcing west source-dir check")
    if build_dir_reset:
        print("  note        = removed stale build directory generated on a different host/path")
    print()

    return subprocess.call(args, env=env, cwd=str(PROJECT_DIR))


def cmd_menuconfig(board_cfg: BoardConfig):
    env, host_tools = setup_env()
    west = find_west(env)

    args = west_build_args(board_cfg, west, False, host_tools)
    # Insert -t menuconfig before the "--" separator so it's parsed by
    # west, not forwarded to CMake.
    try:
        sep = args.index("--")
        args[sep:sep] = ["-t", "menuconfig"]
    except ValueError:
        args.extend(["-t", "menuconfig"])

    print(f"Opening menuconfig for {board_cfg.board} ({board_cfg.alias}) …")
    print(f"  build dir = {board_cfg.build_dir}")
    print()

    return subprocess.call(args, env=env, cwd=str(PROJECT_DIR))


def cmd_flash(board_cfg: BoardConfig, runner: str):
    rc = cmd_build(board_cfg)
    if rc != 0:
        return rc

    env, host_tools = setup_env()
    west = find_west(env)

    args = [west, "flash", "-d", str(board_cfg.build_dir), "--skip-rebuild"]
    args.extend(west_runner_args(board_cfg, runner, host_tools))

    print(f"\nFlashing {board_cfg.board} via {runner} …")
    return subprocess.call(args, env=env, cwd=str(PROJECT_DIR))


def cmd_debug(board_cfg: BoardConfig, runner: str):
    rc = cmd_build(board_cfg)
    if rc != 0:
        return rc

    env, host_tools = setup_env()
    west = find_west(env)

    args = [west, "debug", "-d", str(board_cfg.build_dir), "--skip-rebuild"]
    args.extend(west_runner_args(board_cfg, runner, host_tools))

    print(f"\nStarting debugger for {board_cfg.board} via {runner} …")
    return subprocess.call(args, env=env, cwd=str(PROJECT_DIR))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        nargs="?",
        default="build",
        choices=("build", "clean", "menuconfig", "flash", "debug"),
        help="operation to perform",
    )
    parser.add_argument(
        "board",
        nargs="?",
        default="g4",
        help="board target shorthand: g4|f4|h7|h5 or full Zephyr board name",
    )
    parser.add_argument(
        "runner",
        nargs="?",
        help="runner shorthand for flash/debug: jlink|openocd",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    try:
        board_cfg = resolve_board(args.board)
        if args.action == "build":
            return cmd_build(board_cfg, pristine=False)
        if args.action == "clean":
            return cmd_build(board_cfg, pristine=True)
        if args.action == "menuconfig":
            return cmd_menuconfig(board_cfg)
        if args.action == "flash":
            runner = resolve_runner(args.runner, default=board_cfg.default_flash_runner)
            return cmd_flash(board_cfg, runner)
        if args.action == "debug":
            runner = resolve_runner(args.runner, default=DEFAULT_DEBUG_RUNNER)
            return cmd_debug(board_cfg, runner)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Unknown command '{args.action}'.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
