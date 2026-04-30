cmake_minimum_required(VERSION 3.20.0)

# ── ViewAlyzer module path ────────────────────────────────────────────
# Adjust if your ViewAlyzerRecorder directory is somewhere else.
set(VIEWALYZER_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../ViewAlyzerRecorder"
  CACHE PATH "Path to the ViewAlyzerRecorder Zephyr module"
)

list(APPEND ZEPHYR_EXTRA_MODULES
  ${VIEWALYZER_MODULE_PATH}
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nucleo-g4-blinky)

target_sources(app PRIVATE
  src/main.c
)
