# FreeRTOS Example Projects

Standalone FreeRTOS example projects that integrate the ViewAlyzer Recorder.
Each project is a self-contained STM32CubeMX + CMake setup and pulls recorder
sources from `../../../ViewAlyzerRecorder` (the top-level recorder in this
repo).

| Project | Board |
|---------|-------|
| [Nucleo_F103_VA](Nucleo_F103_VA) | Nucleo-F103RB |
| [Nucleo_F446RE](Nucleo_F446RE) | Nucleo-F446RE (with SEGGER RTT) |
| [Nucleo_G474_VA](Nucleo_G474_VA) | Nucleo-G474RE |
| [Nucleo_U385](Nucleo_U385) | Nucleo-U385 |

Typical build:

```bash
cd Nucleo_G474_VA
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

See each project's directory for board-specific notes.
