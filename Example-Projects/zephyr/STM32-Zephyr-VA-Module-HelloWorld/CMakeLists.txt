cmake_minimum_required(VERSION 3.20.0)

# ── ViewAlyzer module path ────────────────────────────────────────────
# Default assumes the standard ViewAlyzer repo layout:
#   ViewAlyzer/Example-Projects/zephyr/<this-project>/
#   ViewAlyzer/ViewAlyzerRecorder/
# Override on the command line if your layout differs:
#   west build -b nucleo_g474re . -- -DVIEWALYZER_MODULE_PATH=/path/to/ViewAlyzerRecorder
set(VIEWALYZER_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../ViewAlyzerRecorder"
  CACHE PATH "Path to the ViewAlyzerRecorder Zephyr module"
)

list(APPEND ZEPHYR_EXTRA_MODULES
  ${VIEWALYZER_MODULE_PATH}
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(viewalyzer_helloworld)

target_sources(app PRIVATE
  src/main.c
)
