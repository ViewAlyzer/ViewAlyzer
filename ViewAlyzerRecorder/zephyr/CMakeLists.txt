if(CONFIG_VIEWALYZER)
  get_filename_component(VIEWALYZER_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

  zephyr_library()

  zephyr_library_sources(
    ${VIEWALYZER_MODULE_DIR}/core/ViewAlyzer.c
    ${VIEWALYZER_MODULE_DIR}/zephyr/VA_Adapter_Zephyr.c
  )

  zephyr_compile_definitions(
    VA_ENABLED=1
    VA_RTOS_SELECT=2
    VA_MAX_TASKS=${CONFIG_VIEWALYZER_MAX_TASKS}
    VA_MAX_SYNC_OBJECTS=${CONFIG_VIEWALYZER_MAX_SYNC_OBJECTS}
    VA_MAX_USER_FUNCTIONS=${CONFIG_VIEWALYZER_MAX_USER_FUNCTIONS}
    VA_MAX_TASK_NAME_LEN=${CONFIG_VIEWALYZER_MAX_TASK_NAME_LEN}
  )

  if(CONFIG_VIEWALYZER_ALLOW_DISABLE_INTERRUPTS)
    zephyr_compile_definitions(VA_ALLOWED_TO_DISABLE_INTERRUPTS=1)
  else()
    zephyr_compile_definitions(VA_ALLOWED_TO_DISABLE_INTERRUPTS=0)
  endif()

  zephyr_include_directories(
    ${VIEWALYZER_MODULE_DIR}/core
    ${VIEWALYZER_MODULE_DIR}/zephyr
  )

  zephyr_include_directories(BEFORE ${VIEWALYZER_MODULE_DIR}/zephyr)

  zephyr_library_include_directories(
    ${VIEWALYZER_MODULE_DIR}/core
    ${VIEWALYZER_MODULE_DIR}/zephyr
  )
endif()
