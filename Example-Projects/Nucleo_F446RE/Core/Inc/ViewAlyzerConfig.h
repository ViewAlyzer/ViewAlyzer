#ifndef ViewAlyzer_CONFIG_H
#define ViewAlyzer_CONFIG_H
#if defined(VA_ENABLED) && (VA_ENABLED == 1) && (VA_TRACE_FREERTOS == 1)
// Suggesteed FreeRTOS Configs
#define configRECORD_STACK_HIGH_ADDRESS 1

// --- FreeRTOS Trace Macro Definitions ---
// These macros hook into FreeRTOS to capture events using only REAL FreeRTOS trace macros

// Task tracing - these are the actual FreeRTOS macros
#define traceTASK_SWITCHED_IN() va_taskswitchedin()
#define traceTASK_SWITCHED_OUT() va_taskswitchedout()

// Enhanced task creation macro that captures TCB information
// This hack extracts information from the TCB before calling our profiler
extern volatile void *g_task_pxStack;
extern volatile void *g_task_pxEndOfStack;
extern volatile uint32_t g_task_uxPriority;
extern volatile uint32_t g_task_uxBasePriority;
extern volatile uint32_t g_task_ulStackDepth;

#if (configRECORD_STACK_HIGH_ADDRESS == 1)
#define CALCULATE_STACK_DEPTH(pxTCB) \
    g_task_ulStackDepth = (uint32_t)((pxTCB)->pxEndOfStack - (pxTCB)->pxStack)
#else
#define CALCULATE_STACK_DEPTH(pxTCB) \
    g_task_ulStackDepth = 0
#endif

#define traceTASK_CREATE(pxNewTCB)                                   \
    do                                                               \
    {                                                                \
        g_task_pxStack = (pxNewTCB)->pxStack;                        \
        g_task_uxPriority = (pxNewTCB)->uxPriority;                  \
        g_task_pxEndOfStack = NULL;                                  \
        g_task_uxBasePriority = (pxNewTCB)->uxPriority;              \
        CALCULATE_STACK_DEPTH(pxNewTCB);                             \
        va_taskcreated(pxNewTCB);                                    \
    } while (0)

extern volatile uint32_t notificationValue;
#define traceTASK_NOTIFY() (notificationValue = ulValue, va_logtasknotifygive(pxTCB, ulValue))
#define traceTASK_NOTIFY_FROM_ISR() (notificationValue = ulValue, va_logtasknotifygive(pxTCB, ulValue))
#define traceTASK_NOTIFY_GIVE_FROM_ISR() (notificationValue = pxTCB->ulNotifiedValue, va_logtasknotifygive(pxTCB, pxTCB->ulNotifiedValue))
#define traceTASK_NOTIFY_TAKE() va_logtasknotifytake(pxCurrentTCB->ulNotifiedValue)

// Queue tracing - unified approach using generic queue creation macro
// FreeRTOS uses queues as the underlying mechanism for mutexes, semaphores, and queues
// VA uses a single hook that inspects the ucQueueType field to determine object type
// Note: For mutexes, traceCREATE_MUTEX() is called AFTER traceQUEUE_CREATE(), so we need
// to avoid double registration
#define traceQUEUE_CREATE(pxNewQueue) \
    va_logQueueObjectCreateWithType((pxNewQueue), "Queue")

#define traceQUEUE_SEND(pxQueue) \
    va_logQueueObjectGive((pxQueue), xTicksToWait)

#define traceQUEUE_SEND_FROM_ISR(pxQueue) \
    va_logQueueObjectGive((pxQueue), 0)

#define traceQUEUE_RECEIVE(pxQueue) \
    va_logQueueObjectTake((pxQueue), xTicksToWait)

#define traceQUEUE_RECEIVE_FROM_ISR(pxQueue) \
    va_logQueueObjectTake((pxQueue), 0)

// Mutex tracing - traceCREATE_MUTEX is called AFTER traceQUEUE_CREATE in FreeRTOS
// We need to update the existing entry to reflect the correct type
#define traceCREATE_MUTEX(pxNewMutex) \
    va_updateQueueObjectType((pxNewMutex), "Mutex")

#define traceGIVE_MUTEX_RECURSIVE(pxMutex) \
    va_logQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX_RECURSIVE(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)

// Additional mutex operations
#define traceGIVE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    va_logQueueObjectTake((pxMutex), 0)  // Log as failed take

#define traceTAKE_MUTEX_RECURSIVE_FAILED(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)  // Log the attempt

// Standard mutex operations (non-recursive)
#define traceCREATE_MUTEX_FAILED() \
    ((void)0)  // Could log creation failure if needed

#define traceGIVE_MUTEX(pxMutex) \
    va_logQueueObjectGive((pxMutex), 0)

#define traceTAKE_MUTEX(pxMutex) \
    va_logQueueObjectTake((pxMutex), xTicksToWait)

// Blocking trace - called BEFORE task blocks waiting for mutex/semaphore/queue
// This is where we can detect contention since the mutex is still held by another task
#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) \
    va_logQueueObjectBlocking((pxQueue))

// Semaphore operations - these are also queues but we can add specific tracing
// Binary semaphores are created through xSemaphoreCreateBinary() which calls xQueueCreateBinary()
// Counting semaphores are created through xSemaphoreCreateCounting() which calls xQueueCreateCountingSemaphore()
// Both eventually call xQueueGenericCreate() which triggers traceQUEUE_CREATE()

// TODO : Future improvement
// However, we can add specific semaphore creation hooks if FreeRTOS provides them
#define traceCREATE_COUNTING_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#define traceCREATE_BINARY_SEMAPHORE() \
    ((void)0)  // This gets called after traceQUEUE_CREATE, so we can use it to update the name

#endif
#endif // ViewAlyzer_CONFIG_H