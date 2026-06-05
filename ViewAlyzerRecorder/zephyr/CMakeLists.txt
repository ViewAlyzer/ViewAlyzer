if(CONFIG_VIEWALYZER)
  get_filename_component(VIEWALYZER_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

  if(CONFIG_VIEWALYZER_TRANSPORT_RTT)
    set(VIEWALYZER_TRANSPORT_VALUE 2)
  else()
    set(VIEWALYZER_TRANSPORT_VALUE 1)
  endif()

  zephyr_library()

  zephyr_library_sources(
    ${VIEWALYZER_MODULE_DIR}/core/ViewAlyzer.c
    ${VIEWALYZER_MODULE_DIR}/zephyr/VA_Adapter_Zephyr.c
  )

  zephyr_compile_definitions(
    VA_ENABLED=1
    VA_RTOS_SELECT=2
    VA_TRANSPORT=${VIEWALYZER_TRANSPORT_VALUE}
    VA_CAPTURE_STACK_USAGE=$<IF:$<BOOL:${CONFIG_VIEWALYZER_STACK_USAGE}>,1,0>
    VA_MAX_TASKS=${CONFIG_VIEWALYZER_MAX_TASKS}
    VA_MAX_SYNC_OBJECTS=${CONFIG_VIEWALYZER_MAX_SYNC_OBJECTS}
    VA_MAX_USER_FUNCTIONS=${CONFIG_VIEWALYZER_MAX_USER_FUNCTIONS}
    VA_MAX_TASK_NAME_LEN=${CONFIG_VIEWALYZER_MAX_TASK_NAME_LEN}
    VA_AUTO_SETUP_INTERVAL_MS=${CONFIG_VIEWALYZER_AUTO_SETUP_INTERVAL_MS}
  )

  if(CONFIG_VIEWALYZER_TRANSPORT_RTT)
    zephyr_compile_definitions(
      VA_RTT_CHANNEL=${CONFIG_VIEWALYZER_RTT_CHANNEL}
      VA_CONFIGURE_RTT=$<IF:$<BOOL:${CONFIG_VIEWALYZER_CONFIGURE_RTT}>,1,0>
      VA_RTT_BUFFER_SIZE=${CONFIG_VIEWALYZER_RTT_BUFFER_SIZE}u
      VA_RTT_MODE=${CONFIG_SEGGER_RTT_MODE}
    )
  endif()

  if(CONFIG_VIEWALYZER_ALLOW_DISABLE_INTERRUPTS)
    zephyr_compile_definitions(VA_ALLOWED_TO_DISABLE_INTERRUPTS=1)
  else()
    zephyr_compile_definitions(VA_ALLOWED_TO_DISABLE_INTERRUPTS=0)
  endif()

  # Do NOT globally force-include tracing_user.h. It is redundant: kernel.h ->
  # tracing/tracing.h does `#include "tracing_user.h"` under CONFIG_TRACING_USER,
  # and the `include_directories(BEFORE ...)` below makes our copy win (same
  # _TRACE_USER_H guard as the stock header). A global -include also leaks
  # <zephyr/kernel.h> into Zephyr's own bootstrap TUs; on Zephyr >= 4.3 that
  # breaks the lib/heap heap_constants.c step (it runs before kernel.h's
  # generated <zephyr/heap_constants.h> exists). The BEFORE include path wires
  # the recorder identically on 4.1 and 4.3 without that hazard.

  zephyr_include_directories(
    ${VIEWALYZER_MODULE_DIR}/core
    ${VIEWALYZER_MODULE_DIR}/zephyr
  )

  include_directories(BEFORE ${VIEWALYZER_MODULE_DIR}/zephyr)

  zephyr_library_include_directories(
    ${VIEWALYZER_MODULE_DIR}/core
    ${VIEWALYZER_MODULE_DIR}/zephyr
  )
endif()
