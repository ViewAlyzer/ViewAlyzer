# STM32 Zephyr ViewAlyzer Hello World

The smallest Zephyr application that pulls in the ViewAlyzer Recorder as an
external module. It blinks the on-board LED and logs a single user-trace
counter over ITM/SWO so you can confirm the recorder is alive end-to-end.

For a heavier sample with multiple boards, schemas, mutex/queue/PI demos, and
a `build.py` wrapper, see
[../STM32-Zephyr-VA-Module-Full-Demo](../STM32-Zephyr-VA-Module-Full-Demo).

## What's in here

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Adds `ViewAlyzerRecorder` via `ZEPHYR_EXTRA_MODULES` |
| `prj.conf` | Enables `CONFIG_VIEWALYZER` + `CONFIG_TRACING_USER` and selects ITM/SWO |
| `src/main.c` | ~40 lines: init recorder, blink LED, log a counter |

That's the entire integration. No board overlays, no schemas, no build
script — just `west build`.

## Hardware

- ST Nucleo `nucleo_g474re`

If you want a different board, change the `-b` argument and add a transport
config to `prj.conf` (see `prj.conf` in the Full-Demo example for ITM/SWO and
RTT variants).

## Requirements

- A Zephyr workspace with `zephyr/` checked out somewhere on disk
- `west` installed
- An ARM toolchain that Zephyr can find (e.g. STM32CubeCLT GNU Arm Embedded
  with `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb` and `GNUARMEMB_TOOLCHAIN_PATH`
  set, or the Zephyr SDK)
- The `ViewAlyzerRecorder` directory available on disk (it ships with the
  ViewAlyzer repo at `ViewAlyzer/ViewAlyzerRecorder`)

## Build

From this directory:

```bash
west build -b nucleo_g474re .
```

If your `ViewAlyzerRecorder` directory is not at the default relative path
(`../../../ViewAlyzerRecorder`), point `CMakeLists.txt` at it on the command
line:

```bash
west build -b nucleo_g474re . -- \
  -DVIEWALYZER_MODULE_PATH=/absolute/path/to/ViewAlyzerRecorder
```

## Flash

```bash
west flash
```

Use `--runner jlink` or `--runner openocd` to pick a specific runner.

## Verify in the desktop app

1. Start a SWV session against the G4 at the SWO frequency in `prj.conf`
   (`2 MHz`).
2. Open the ViewAlyzer desktop app and connect to the same source.
3. You should see:
   - thread-switch activity (Zephyr `idle`, `main`, `sysworkq`, ...)
   - a `Counter` user trace incrementing once per blink

If thread switches show up but the counter does not, double-check that
both `CONFIG_VIEWALYZER=y` **and** `CONFIG_TRACING_USER=y` are set in
`prj.conf` — that pair is the easiest thing to miss.

## Where to go next

- Add another user trace: pick a new ID, call `VA_RegisterUserTrace(...)`,
  then `VA_LogTrace(id, value)` from anywhere
- Switch to RTT: see `boards/nucleo_f446re.conf` in the Full-Demo
- See the recorder's own [Zephyr README](../../../ViewAlyzerRecorder/zephyr/README.md)
  for the full Kconfig surface
