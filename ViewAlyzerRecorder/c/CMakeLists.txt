cmake_minimum_required(VERSION 3.14)
project(ViewAlyzerSDK VERSION 0.1.0 LANGUAGES C)

# ── Core library (traces, strings, toggles, functions) ───────────────────
add_library(viewalyzer_core STATIC
    viewalyzer_cobs.c
    viewalyzer_udp.c
)
target_include_directories(viewalyzer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(viewalyzer_core PUBLIC ws2_32)
endif()

# ── RTOS extension library (tasks, ISRs, sync objects) ───────────────────
add_library(viewalyzer_rtos STATIC
    viewalyzer_udp_rtos.c
)
target_include_directories(viewalyzer_rtos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(viewalyzer_rtos PUBLIC viewalyzer_core)

# ── Desktop example (core only) ─────────────────────────────────────────
add_executable(desktop_example examples/desktop_example.c)
target_link_libraries(desktop_example PRIVATE viewalyzer_core)

if(UNIX)
    target_link_libraries(desktop_example PRIVATE m)
endif()
