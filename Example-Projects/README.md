# Example Projects

Standalone, self-contained example projects that demonstrate how to integrate
the **ViewAlyzer Recorder** into your firmware. Each subproject is intended to
be cloned (or copied) and built without needing any other configuration beyond
the standard toolchain for that target.

The projects are grouped by RTOS / runtime:

```
Example-Projects/
├── baremetal/        # No RTOS — direct user trace + ISR instrumentation
├── freertos/         # FreeRTOS-based examples (task switches, sync objects)
├── zephyr/           # Zephyr RTOS examples using the ViewAlyzer Zephyr module
└── Desktop-CPP-UDP/  # Host-side C++ example that streams traces over UDP
```

> Not every category has an example yet. New ones are added as boards and
> RTOS support are validated. Contributions welcome.

All firmware projects pull recorder sources from the shared
[../ViewAlyzerRecorder/](../ViewAlyzerRecorder/) directory using a relative
path of `../../../ViewAlyzerRecorder` from each project's `CMakeLists.txt`.
This keeps every example pointing at a single copy of the recorder source.

---

## baremetal/

Bare-metal projects use only `core/ViewAlyzer.c` (and `viewalyzer_cobs.c` if a
framed custom transport is needed). They demonstrate user traces, events, and
ISR instrumentation without any RTOS hooks.

*No projects yet — coming soon.*

---

## freertos/

FreeRTOS projects pull in the `core/` recorder plus the FreeRTOS adapter
(`freertos/VA_Adapter_FreeRTOS.c`) and enable trace macros via
`VA_TRACE_FREERTOS=1` so task switches, queues, semaphores, and mutexes are
captured automatically.

| Project | Board | Notes |
|---------|-------|-------|
| [Nucleo_F103_VA](freertos/Nucleo_F103_VA) | Nucleo-F103RB | STM32CubeMX + CMake, FreeRTOS |
| [Nucleo_F446RE](freertos/Nucleo_F446RE) | Nucleo-F446RE | STM32CubeMX + CMake, FreeRTOS, SEGGER RTT |
| [Nucleo_G474_VA](freertos/Nucleo_G474_VA) | Nucleo-G474RE | STM32CubeMX + CMake, FreeRTOS |
| [Nucleo_U385](freertos/Nucleo_U385) | Nucleo-U385 | STM32CubeMX + CMake, FreeRTOS |

Build (typical):

```bash
cd freertos/Nucleo_G474_VA
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

---

## zephyr/

Zephyr projects use the recorder as an external Zephyr module. They are
**standalone Zephyr applications** and assume that a Zephyr workspace is
already available on disk.

| Project | Boards | Notes |
|---------|--------|-------|
| [STM32-Zephyr-VA-Module-Sample](zephyr/STM32-Zephyr-VA-Module-Sample) | `nucleo_g474re`, `nucleo_f446re`, `nucleo_h503rb`, `stm32h750b_dk` | Wraps `west` via `build.py`, supports ITM/SWO and J-Link RTT |

### Expected Zephyr Layout

The Zephyr examples expect a Zephyr checkout to live **above** the project on
disk. The recommended layout is to put the Zephyr source as a sibling of the
`ViewAlyzer` repo (any of the typical `west init` layouts will work):

```
<your-workspace>/
├── ViewAlyzer/                 # this repository
│   └── Example-Projects/
│       └── zephyr/
│           └── STM32-Zephyr-VA-Module-Sample/
└── zephyr/                     # Zephyr RTOS source tree (contains west.yml,
                                #   kernel/, include/, boards/, ...)
```

The sample's `build.py` will automatically locate Zephyr by:

1. Honoring the `ZEPHYR_BASE_OVERRIDE` constant inside `build.py`.
2. Honoring the `ZEPHYR_BASE` environment variable.
3. Otherwise walking up from the project directory looking for a sibling
   folder named `zephyr/` that contains a `west.yml`.

If you cloned this repo at a non-standard location, set `ZEPHYR_BASE` or edit
`ZEPHYR_BASE_OVERRIDE` in `build.py`. See the project's own
[README](zephyr/STM32-Zephyr-VA-Module-Sample/README.md) and
[TUTORIAL](zephyr/STM32-Zephyr-VA-Module-Sample/TUTORIAL.md) for details.

Build (typical):

```bash
cd zephyr/STM32-Zephyr-VA-Module-Sample
python3 build.py            # default G4 build
python3 build.py build h7   # H7 build
python3 build.py flash g4   # flash via OpenOCD/ST-Link
```

---

## Desktop-CPP-UDP/

Host-side C++ example that uses the `ViewAlyzerRecorder/c` UDP sender library.
Useful for desktop simulation, prototyping, or producing traces from a
non-embedded source. Build with any standard C++17 toolchain:

```bash
cd Desktop-CPP-UDP
cmake -S . -B build
cmake --build build
```

---

## License

See the repository [LICENSE](../LICENSE).
