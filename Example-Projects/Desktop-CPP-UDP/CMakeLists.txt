cmake_minimum_required(VERSION 3.14)
project(DesktopCppUdpExample VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ViewAlyzer core library (COBS + UDP sender)
add_library(viewalyzer_core STATIC
    ../../ViewAlyzerRecorder/c/viewalyzer_cobs.c
    ../../ViewAlyzerRecorder/c/viewalyzer_udp.c
)
target_include_directories(viewalyzer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ViewAlyzerRecorder/c
)
if(WIN32)
    target_link_libraries(viewalyzer_core PUBLIC ws2_32)
endif()

# ViewAlyzer RTOS extension
add_library(viewalyzer_rtos STATIC
    ../../ViewAlyzerRecorder/c/viewalyzer_udp_rtos.c
)
target_include_directories(viewalyzer_rtos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ViewAlyzerRecorder/c
)
target_link_libraries(viewalyzer_rtos PUBLIC viewalyzer_core)

# Example executable
add_executable(desktop_cpp_udp_example main.cpp)
target_link_libraries(desktop_cpp_udp_example PRIVATE viewalyzer_core viewalyzer_rtos)

if(UNIX)
    target_link_libraries(desktop_cpp_udp_example PRIVATE m)
endif()
