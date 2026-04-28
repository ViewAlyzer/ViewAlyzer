# Zephyr Example Projects

Standalone Zephyr applications that use the ViewAlyzer Recorder as an
external Zephyr module. They expect a Zephyr checkout on disk; see
[../README.md](../README.md#zephyr) for the recommended layout.

| Project | Boards |
|---------|--------|
| [STM32-Zephyr-VA-Module-Sample](STM32-Zephyr-VA-Module-Sample) | `nucleo_g474re`, `nucleo_f446re`, `nucleo_h503rb`, `stm32h750b_dk` |

## Expected Layout

These projects assume a `zephyr/` directory exists somewhere above this
project. The recommended layout is a sibling of the `ViewAlyzer` repo:

```
<your-workspace>/
├── ViewAlyzer/
│   └── Example-Projects/zephyr/<this-project>/
└── zephyr/        # Zephyr source tree (west.yml, kernel/, boards/, ...)
```

Override with the `ZEPHYR_BASE` environment variable or
`ZEPHYR_BASE_OVERRIDE` inside the project's `build.py` if your layout
differs.
