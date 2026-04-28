# Nucleo Zephyr Module

For a follow-along setup guide, see [TUTORIAL.md](TUTORIAL.md).

This project builds a Zephyr application for four supported STM32 boards:

- `g4` -> `nucleo_g474re`
- `f4` -> `nucleo_f446re`
- `h7` -> `stm32h750b_dk`
- `h5` -> `nucleo_h503rb`

The main entry point is `build.py`, which wraps `west`, auto-detects whether it is running on Windows or Linux, and sets the required toolchain environment automatically.

## Requirements

- A Zephyr workspace with `zephyr` checked out somewhere on your system.
- `west` installed in the user environment.
- STM32CubeCLT GNU Arm Embedded toolchain installed in a standard location.
- The `ViewAlyzerRecorder` directory (ships with ViewAlyzer) available on disk.
- For J-Link flashing: SEGGER J-Link software installed, plus the Python package `pylink-square` available to the Python interpreter that runs `west`.
- On Windows, `west build -t menuconfig` also needs the Python package `windows-curses` installed in the same interpreter that runs `west`.

## Quick Setup

1. Open `build.py` and set `ZEPHYR_BASE_OVERRIDE` to point at your Zephyr checkout.
2. Verify the `VIEWALYZER_MODULE_PATH` in `CMakeLists.txt` resolves to your `ViewAlyzerRecorder` directory (the default assumes the standard ViewAlyzer repo layout).
3. Run `python3 build.py` to build for the default G4 target.

Optional environment variable overrides:

- `STM32CUBECLT_ROOT`
- `GNUARMEMB_TOOLCHAIN_PATH`
- `OPENOCD_BIN`
- `OPENOCD_SCRIPTS`
- `JLINK_COMMANDER`

If these are not set, `build.py` searches common install locations on both Windows and Linux.

## Command Syntax

```bash
python3 build.py [action] [board] [runner]
```

Positional arguments:

- `action`: optional. One of `build`, `clean`, `flash`, `debug`.
- `board`: optional. One of the supported board aliases or full board names.
- `runner`: optional. Only used by `flash` and `debug`.

If omitted, the defaults are:

- `action = build`
- `board = g4`
- `flash runner = openocd` for `g4`
- `flash runner = jlink` for `f4`
- `flash runner = openocd` for `h7`
- `flash runner = openocd` for `h5`
- `debug runner = openocd`

## Actions

### `build`

Performs an incremental build.

Examples:

```bash
python3 build.py
python3 build.py build
python3 build.py build g4
python3 build.py build f4
python3 build.py build h7
python3 build.py build h5
```

### `clean`

Performs a pristine rebuild by passing `-p always` to `west build`.

Examples:

```bash
python3 build.py clean g4
python3 build.py clean f4
python3 build.py clean h7
python3 build.py clean h5
```

### `flash`

Builds first, then flashes the image with the selected runner.

Default runner: board-specific

- `g4` -> `openocd`
- `f4` -> `jlink`
- `h7` -> `openocd`
- `h5` -> `openocd`

Examples:

```bash
python3 build.py flash g4
python3 build.py flash f4
python3 build.py flash h7
python3 build.py flash h5
python3 build.py flash g4 stlink
python3 build.py flash g4 jlink
python3 build.py flash f4 jlink
python3 build.py flash f4 openocd
python3 build.py flash h7 stlink
python3 build.py flash h5 stlink
```

### `debug`

Builds first, then starts the selected debugger runner.

Default runner: `openocd`

Examples:

```bash
python3 build.py debug g4
python3 build.py debug g4 openocd
python3 build.py debug f4 jlink
python3 build.py debug h7
python3 build.py debug h5
```

## Supported Boards

These values are accepted for the `board` argument:

| Input | Resolved board | Build directory | J-Link device |
| --- | --- | --- | --- |
| `g4` | `nucleo_g474re` | `build/` | `STM32G474RE` |
| `g474` | `nucleo_g474re` | `build/` | `STM32G474RE` |
| `nucleo_g474re` | `nucleo_g474re` | `build/` | `STM32G474RE` |
| `f4` | `nucleo_f446re` | `build-f4/` | `STM32F446RE` |
| `f446` | `nucleo_f446re` | `build-f4/` | `STM32F446RE` |
| `nucleo_f446re` | `nucleo_f446re` | `build-f4/` | `STM32F446RE` |
| `h7` | `stm32h750b_dk` | `build-h7/` | `STM32H735IG` |
| `h750` | `stm32h750b_dk` | `build-h7/` | `STM32H735IG` |
| `stm32h750b_dk` | `stm32h750b_dk` | `build-h7/` | `STM32H735IG` |
| `h5` | `nucleo_h503rb` | `build-h5/` | `STM32H503RB` |
| `h503` | `nucleo_h503rb` | `build-h5/` | `STM32H503RB` |
| `nucleo_h503rb` | `nucleo_h503rb` | `build-h5/` | `STM32H503RB` |

Separate build directories are used so the G4, F4, H7, and H5 configurations do not overwrite each other.

## Supported Runners

These values are accepted for the `runner` argument:

| Input | Resolved runner | Used by |
| --- | --- | --- |
| `jlink` | `jlink` | `flash`, `debug` |
| `jl` | `jlink` | `flash`, `debug` |
| `stlink` | `openocd` | `flash`, `debug` |
| `st-link` | `openocd` | `flash`, `debug` |
| `st` | `openocd` | `flash`, `debug` |
| `openocd` | `openocd` | `flash`, `debug` |
| `ocd` | `openocd` | `flash`, `debug` |

## What `build.py` Does Internally

`build.py` sets these environment variables before invoking `west`:

- `ZEPHYR_BASE`
- `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb`
- `GNUARMEMB_TOOLCHAIN_PATH`

It also removes `ZEPHYR_SDK_INSTALL_DIR` from the environment to avoid CMake picking up an incompatible Zephyr SDK.

If the repository root contains generated in-source build artifacts such as `CMakeCache.txt`, `CMakeFiles/`, or a generated `Kconfig/` directory, `build.py` moves them into `.root-artifact-backup/` before building.

If an existing `build/`, `build-f4/`, `build-h7/`, or `build-h5/` directory was copied from another machine, OS, or workspace path, `build.py` removes that stale build directory and lets `west` regenerate it with the current host paths.

## Equivalent `west` Behavior

### Build

For a normal build, `build.py` runs the equivalent of:

```bash
west build -p auto -b <board> -d <build-dir> . -- \
  -DZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
  -DGNUARMEMB_TOOLCHAIN_PATH=<auto-detected-toolchain-path>
```

For a clean build, it uses `-p always` instead of `-p auto`.

If needed, `build.py` also adds `--force` before `build` when the source directory still looks like a CMake build directory after cleanup.

### Flash with J-Link

For J-Link flashing, `build.py` runs the equivalent of:

```bash
west flash -d <build-dir> --skip-rebuild --runner jlink -- \
  --device <segger-device-name> \
  --commander <auto-detected-JLink-commander> \
  --tool-opt=-autoconnect\ 1
```

### Flash with OpenOCD

For OpenOCD flashing, `build.py` runs the equivalent of:

```bash
west flash -d <build-dir> --skip-rebuild --runner openocd \
  --openocd <auto-detected-openocd-binary> \
  --openocd-search <auto-detected-openocd-scripts>
```

### Menuconfig

Opens the Kconfig interactive configuration UI:

```bash
python3 build.py menuconfig g4
python3 build.py menuconfig f4
python3 build.py menuconfig h5
```

On Windows, you also need the `windows-curses` Python package installed.

## J-Link Notes

- The J-Link runner requires the Python module `pylink` from the `pylink-square` package.
- If `JLinkExe` reports `Cannot connect to J-Link`, that is a probe visibility issue, not a build.py syntax problem.
- On Linux, the probe should enumerate as a SEGGER device for J-Link flashing to work.
- If the probe still shows up as an ST-LINK device in `lsusb`, J-Link flashing will fail even though the build command is valid.

## ST-LINK Notes

- The default `flash` runner for `g4` is `openocd`, which is the intended path for the onboard Nucleo ST-LINK probe.
- `stlink`, `st-link`, and `st` are accepted aliases and resolve to the `openocd` runner.
- The G4 OpenOCD config falls back from `swd` to `hla_swd` so it works with both newer ST OpenOCD builds and older/xPack ST-LINK transports.
- If OpenOCD reports `open failed`, the host is not seeing the ST-LINK probe or cannot access it.

## Board Default Notes

- `g4` defaults to `openocd` because it is normally flashed through the onboard ST-LINK path.
- `f4` defaults to `jlink` because this setup is intended to use a SEGGER J-Link on that board.
- `h7` defaults to `openocd` because the STM32H750B-DK includes an onboard ST-LINK probe.
- `h5` defaults to `openocd` because the Nucleo-H503RB includes an onboard ST-LINK probe (ST-Link only).

## Quick Reference

```bash
# Default build for G4
python3 build.py

# Incremental builds
python3 build.py build g4
python3 build.py build f4
python3 build.py build h7
python3 build.py build h5

# Clean builds
python3 build.py clean g4
python3 build.py clean f4
python3 build.py clean h7
python3 build.py clean h5

# Flash
python3 build.py flash g4
python3 build.py flash f4
python3 build.py flash h7
python3 build.py flash h5
python3 build.py flash g4 stlink
python3 build.py flash f4 jlink
python3 build.py flash h7 stlink
python3 build.py flash h5 stlink
python3 build.py flash f4 openocd

# Debug
python3 build.py debug g4
python3 build.py debug f4 jlink
python3 build.py debug h7
python3 build.py debug h5
```