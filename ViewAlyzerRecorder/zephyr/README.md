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

#enable SWO
CONFIG_LOG=y
CONFIG_LOG_BACKEND_SWO=y
# Deferred mode is recommended for performance
CONFIG_LOG_MODE_DEFERRED=y
# Optionally set the SWO frequency (e.g., 875000 Hz or 0 for max)
CONFIG_LOG_BACKEND_SWO_FREQ_HZ=2000000
```