# ViewAlyzer Zephyr Module Tutorial

This tutorial shows two closely related workflows:

1. Create a Zephyr application that uses ViewAlyzer as an external module, like this `STM32-Zephyr-VA-Module-Sample` project.
2. Add the same module integration to an existing Zephyr application.

The goal is to give you something you can follow line by line and reproduce without guessing which pieces matter.

## What This Project Is Doing

This project is not copying the recorder sources into the app.

Instead, it points Zephyr at the ViewAlyzer module root:

- app source lives here in `STM32-Zephyr-VA-Module-Sample`
- ViewAlyzer module source lives in `ViewAlyzer/ViewAlyzerRecorder`
- Zephyr discovers that module through `ZEPHYR_EXTRA_MODULES`

That is the key idea. Once Zephyr sees the module, the module's own `zephyr/module.yml`, `zephyr/Kconfig`, and `zephyr/CMakeLists.txt` do the rest.

## Before You Start

You need:

- a working Zephyr workspace
- `west` installed
- a working ARM toolchain
- the `ViewAlyzer/ViewAlyzerRecorder` directory available somewhere on disk

If you want RTT transport, you also need the Zephyr `segger` module available in the active west workspace.

## The Minimum Pieces

To recreate this style of project, you need five things:

1. `ZEPHYR_EXTRA_MODULES` pointing at `ViewAlyzerRecorder`
2. `CONFIG_VIEWALYZER=y` in `prj.conf`
3. `CONFIG_TRACING_USER=y` in `prj.conf`
4. application code that includes `ViewAlyzer.h`
5. board or app config selecting ITM/SWO or RTT

Everything else is support around those five pieces.

## Part 1: Create a Fresh Module-Based App

### Step 1: Create the App Skeleton

Start with a normal Zephyr app directory:

```text
my-zephyr-app/
  CMakeLists.txt
  prj.conf
  src/
    main.c
  boards/
```

The project in this workspace follows that same shape.

### Step 2: Point Zephyr at the ViewAlyzer Module

Edit your app `CMakeLists.txt` and append the module path before `find_package(Zephyr ...)`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/path/to/ViewAlyzerRecorder
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my-zephyr-app)

target_sources(app PRIVATE
  src/main.c
)
```

What matters here:

- the path must point at the module root, which is `ViewAlyzerRecorder`
- it must be added before `find_package(Zephyr ...)`
- the path can be absolute or relative

If your app already builds, this is usually the only CMake change you need.

### Step 3: Enable the Module in `prj.conf`

Start with this minimal config:

```conf
CONFIG_GPIO=y

CONFIG_VIEWALYZER=y
CONFIG_TRACING_USER=y

CONFIG_MULTITHREADING=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_STACK_INFO=y
```

Important detail:

`CONFIG_VIEWALYZER=y` is not enough by itself. You also need `CONFIG_TRACING_USER=y` so Zephyr routes tracing callbacks through the user hook path that the module uses.

That is one of the easiest things to miss.

### Step 4: Pick a Transport

Pick one transport in either `prj.conf` or a board-specific config fragment.

For ITM/SWO:

```conf
CONFIG_VIEWALYZER_TRANSPORT_ITM=y
CONFIG_VIEWALYZER_TRANSPORT_RTT=n
CONFIG_LOG_BACKEND_SWO=y
CONFIG_LOG_BACKEND_SWO_FREQ_HZ=2000000
CONFIG_STM32_ENABLE_DEBUG_SLEEP_STOP=y
```

For RTT:

```conf
CONFIG_VIEWALYZER_TRANSPORT_ITM=n
CONFIG_VIEWALYZER_TRANSPORT_RTT=y
CONFIG_VIEWALYZER_RTT_CHANNEL=0
CONFIG_VIEWALYZER_CONFIGURE_RTT=y
CONFIG_VIEWALYZER_RTT_BUFFER_SIZE=4096
```

Recommendation:

Keep transport choice in `boards/<board>.conf` if different boards need different transports. That is how this project is structured.

### Step 5: Write a Small `main.c`

Here is a minimal example that proves the module is alive:

```c
#include <zephyr/kernel.h>
#include "ViewAlyzer.h"
#include "VA_Adapter_Zephyr.h"

int main(void)
{
    k_msleep(1000);

    VA_Init(SystemCoreClock);
    VA_Zephyr_RegisterExistingThreads();

    VA_RegisterUserTrace(1, "Counter", VA_USER_TYPE_COUNTER);

    int32_t counter = 0;
    while (1)
    {
        VA_LogTrace(1, counter++);
        k_msleep(500);
    }
}
```

Why each line matters:

- `ViewAlyzer.h` gives you the core recorder API
- `VA_Adapter_Zephyr.h` gives you `VA_Zephyr_RegisterExistingThreads()`
- `VA_Init(SystemCoreClock)` starts the recorder
- `VA_Zephyr_RegisterExistingThreads()` emits setup packets for threads that existed before init

If your app defines threads statically with `K_THREAD_DEFINE`, call `VA_Zephyr_RegisterExistingThreads()` unless you have a strong reason not to.

### Step 6: Build It

You can build with normal Zephyr commands:

```bash
west build -b nucleo_g474re .
```

Or with an explicit build directory:

```bash
west build -b nucleo_g474re -d build-g4 .
```

If the module path and config are correct, Zephyr should pull in:

- `ViewAlyzerRecorder/core/ViewAlyzer.c`
- `ViewAlyzerRecorder/zephyr/VA_Adapter_Zephyr.c`

You do not need to add those source files manually to your app CMake file.

## Part 2: Convert an Existing Zephyr App

If you already have a Zephyr project, do this in order.

### Step 1: Add the Module Path in CMake

Take your existing app `CMakeLists.txt` and add:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
  /absolute/or/relative/path/to/ViewAlyzerRecorder
)
```

Place it before:

```cmake
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

In this example project, the actual CMakeLists.txt uses a CMake cache variable
so you can override the path without editing the file:

```cmake
set(VIEWALYZER_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ViewAlyzerRecorder"
  CACHE PATH "Path to the ViewAlyzerRecorder Zephyr module"
)
```

That is the most important integration step.

### Step 2: Enable Kconfig Options

Add these to your existing `prj.conf`:

```conf
CONFIG_VIEWALYZER=y
CONFIG_TRACING_USER=y
```

Then enable whichever event groups you want. A sensible starting point is:

```conf
CONFIG_VIEWALYZER_TRACE_THREADS=y
CONFIG_VIEWALYZER_TRACE_ISRS=y
CONFIG_VIEWALYZER_TRACE_MUTEXES=y
CONFIG_VIEWALYZER_TRACE_SEMAPHORES=y
CONFIG_VIEWALYZER_TRACE_MESSAGE_QUEUES=y
CONFIG_VIEWALYZER_TRACE_SLEEP=y
CONFIG_VIEWALYZER_STACK_USAGE=y
```

If your project is memory-constrained, start smaller and add features back one at a time.

### Step 3: Initialize the Recorder in App Startup

In your existing app startup path, add:

```c
#include "ViewAlyzer.h"
#include "VA_Adapter_Zephyr.h"
```

Then initialize:

```c
VA_Init(SystemCoreClock);
VA_Zephyr_RegisterExistingThreads();
```

Do this after clocks and low-level debug transport are ready.

### Step 4: Add One User Trace First

Do not start with a large trace plan. Prove the integration with one user trace first:

```c
VA_RegisterUserTrace(1, "LoopCounter", VA_USER_TYPE_COUNTER);
VA_LogTrace(1, loop_counter);
```

Once that appears on the host side, add more traces and let the Zephyr adapter fill in thread and sync-object events.

## Board-Specific Config Pattern

This project keeps shared config in `prj.conf` and per-board transport details in `boards/*.conf`.

That is a good pattern to copy.

Example:

```text
boards/
  nucleo_g474re.conf
  nucleo_f446re.conf
  stm32h750b_dk.conf
  nucleo_h503rb.conf
```

Typical usage:

- G4, H7, H5 use ITM/SWO in this project
- F4 uses RTT in this project

This keeps one app source tree usable across multiple debug transports.

## How This Project Is Wired

These files in this project are the ones worth copying mentally:

- [CMakeLists.txt](CMakeLists.txt) adds `ZEPHYR_EXTRA_MODULES`
- [prj.conf](prj.conf) enables the module and base tracing settings
- [boards/nucleo_g474re.conf](boards/nucleo_g474re.conf) selects ITM/SWO for G4
- [boards/nucleo_f446re.conf](boards/nucleo_f446re.conf) selects RTT for F4
- [boards/stm32h750b_dk.conf](boards/stm32h750b_dk.conf) selects ITM/SWO for H7
- [boards/nucleo_h503rb.conf](boards/nucleo_h503rb.conf) selects ITM/SWO and tighter memory settings for H5
- [src/main.c](src/main.c) shows the application-side includes and `VA_Init()` call
- [src/main.h](src/main.h) is a shim header for CMSIS and STM32 device definitions

## The `main.h` Shim Issue

One important ViewAlyzer detail is that `ViewAlyzer.h` still includes `main.h`.

In a normal STM32Cube project that file already exists. In a Zephyr app, it usually does not.

This project solves that by providing its own `src/main.h` shim with the MCU header include:

```c
#include <stm32g4xx.h>
```

If you move this setup to another STM32 family, update that include to the correct device header.

Examples:

- G4: `stm32g4xx.h`
- F4: `stm32f4xx.h`
- H5: `stm32h5xx.h`
- H7: `stm32h7xx.h`

If your board mix spans multiple STM32 families, the cleaner long-term solution is to use the module-owned shim in `ViewAlyzerRecorder/zephyr/main.h` rather than depending on an app-local `main.h`.

## RTT Gotcha: `segger` Must Exist in the Active Workspace

If you choose RTT transport, the Zephyr `segger` module must be present in the active west workspace.

That means:

- your active `west.yml` must allow `segger` to be imported
- `west update` must actually fetch it into `modules/debug/segger`

If RTT is enabled and `segger` is missing, the build will not complete cleanly.

Do not assume the `west.yml` sitting next to your app is the active one. Check which manifest the current workspace is actually using.

## ITM/SWO Gotcha: Host and Target Baud Must Match

If you use SWO:

- the target-side SWO rate must match the host capture configuration
- `CONFIG_LOG_BACKEND_SWO_FREQ_HZ` must match what the host expects if you also use Zephyr's SWO log backend

If the host cannot decode SWO cleanly, treat that as a transport mismatch first, not as a ViewAlyzer parser problem.

## Debug Attach Timing Gotcha

This project's notes mention that Zephyr plus J-Link may need a short delay before `VA_Init()` so the debugger can attach in time to catch the initial sync marker.

That is why you may see something like:

```c
k_msleep(3000);
VA_Init(SystemCoreClock);
```

If your host attaches after the first sync packet, you may still recover later if auto setup re-emission is enabled, but for bring-up it is often simpler to delay init briefly.

## What to Check If It Does Not Work

Work through these in order:

1. Does your app `CMakeLists.txt` add `ZEPHYR_EXTRA_MODULES` before `find_package(Zephyr ...)`?
2. Does `prj.conf` include both `CONFIG_VIEWALYZER=y` and `CONFIG_TRACING_USER=y`?
3. Did you select exactly one transport?
4. If using RTT, is `modules/debug/segger` present in the active west workspace?
5. Does your app include `ViewAlyzer.h` and call `VA_Init()`?
6. If threads already exist before init, did you call `VA_Zephyr_RegisterExistingThreads()`?
7. If using SWO, does the host use the same frequency as the target?

If you verify those seven items, most integration failures become straightforward.

## Copy-Paste Starter Set

If you want the shortest possible starting point for an existing app, use this checklist.

### `CMakeLists.txt`

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/path/to/ViewAlyzerRecorder
)
```

### `prj.conf`

```conf
CONFIG_VIEWALYZER=y
CONFIG_TRACING_USER=y
CONFIG_VIEWALYZER_TRACE_THREADS=y
CONFIG_VIEWALYZER_TRACE_ISRS=y
CONFIG_VIEWALYZER_TRANSPORT_ITM=y
```

### `main.c`

```c
#include <zephyr/kernel.h>
#include "ViewAlyzer.h"
#include "VA_Adapter_Zephyr.h"

int main(void)
{
    VA_Init(SystemCoreClock);
    VA_Zephyr_RegisterExistingThreads();

    VA_RegisterUserTrace(1, "Counter", VA_USER_TYPE_COUNTER);

    int32_t counter = 0;
    while (1)
    {
        VA_LogTrace(1, counter++);
        k_msleep(500);
    }
}
```

That is enough to prove the module integration is alive.

## Recommended Follow-Through

When you test this tutorial, I would follow this exact order:

1. Build with ITM/SWO on one STM32 board first.
2. Confirm one user trace appears.
3. Confirm Zephyr thread switches appear.
4. Add mutex or queue activity.
5. Only then add RTT or multi-board support.

That sequence keeps failures isolated and easy to reason about.