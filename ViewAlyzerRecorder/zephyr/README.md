### Ssample project config to enable ViewAlyzer

```
# GPIO for LED blinky
CONFIG_GPIO=y

# Kernel features for ViewAlyzer integration
CONFIG_MULTITHREADING=y
CONFIG_NUM_PREEMPT_PRIORITIES=16
CONFIG_SCHED_MULTIQ=y

# Semaphores and Mutexes
CONFIG_POLL=y

# Message queues and FIFOs
CONFIG_HEAP_MEM_POOL_SIZE=8192

# Events
CONFIG_EVENTS=y

# Thread stack sizes
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_IDLE_STACK_SIZE=512

# Math library for sinf()
CONFIG_NEWLIB_LIBC=y

# Serial console for debug output
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_PRINTK=y

# ViewAlyzer tracing via Zephyr module integration
CONFIG_VIEWALYZER=y
CONFIG_TRACING_USER=y

# Transport selection is part of the ViewAlyzer module config.
# Default is ITM/SWO unless overridden by a board-specific .conf.
CONFIG_VIEWALYZER_TRANSPORT_ITM=y

# If using ITM/SWO, enable the Zephyr SWO log backend as needed.
CONFIG_LOG=y
CONFIG_LOG_BACKEND_SWO=y
CONFIG_LOG_MODE_DEFERRED=y
CONFIG_LOG_BACKEND_SWO_FREQ_HZ=2000000

# If using SEGGER RTT instead, add 'segger' to the west manifest
# name-allowlist and run `west update segger`, then select the RTT transport:
# CONFIG_VIEWALYZER_TRANSPORT_RTT=y
# CONFIG_VIEWALYZER_RTT_CHANNEL=0
# CONFIG_VIEWALYZER_CONFIGURE_RTT=y
# CONFIG_VIEWALYZER_RTT_BUFFER_SIZE=4096

# For apps that build multiple boards with different transports, prefer
# board-specific config fragments such as:
#   boards/nucleo_g474re.conf -> CONFIG_VIEWALYZER_TRANSPORT_ITM=y
#   boards/nucleo_f446re.conf -> CONFIG_VIEWALYZER_TRANSPORT_RTT=y
```