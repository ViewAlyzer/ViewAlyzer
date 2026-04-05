#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <math.h>
#include <string.h>
#include "ViewAlyzer.h"
#include "VA_Adapter_Zephyr.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LED0_NODE DT_ALIAS(led0)
#define LED_BLINK_TIME_MS 50

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);


/* Sine wave state */
static volatile uint16_t sineVal;
static volatile uint16_t sine_index;
static volatile uint32_t tick_counter;
static uint16_t get_next_sine_value(void)
{
	float radians = (sine_index * 2.0f * (float)M_PI) / 360.0f;
	float s = sinf(radians);
	volatile uint16_t value = (uint16_t)((s + 1.0f) * 100.0f);
	sine_index = (sine_index + 1) % 360;
	return value;
}

/* Data structure for message queues (like FreeRTOS SensorData_t) */
typedef struct {
	uint32_t sensorValue;
	uint32_t timestamp;
	uint8_t  taskId;
} SensorData_t;

typedef struct {
	uint8_t  profileIndex;
	uint32_t parameter;
} WorkloadCommand_t;

/* Message queues (equivalent to FreeRTOS xQueueCreate) */
K_MSGQ_DEFINE(data_msgq, sizeof(SensorData_t), 10, 4);
K_MSGQ_DEFINE(command_msgq, sizeof(WorkloadCommand_t), 5, 4);

/* Mutexes (equivalent to FreeRTOS xSemaphoreCreateMutex) */
K_MUTEX_DEFINE(shared_resource_mutex);
K_MUTEX_DEFINE(print_mutex);
K_MUTEX_DEFINE(contention_test_mutex);
K_MUTEX_DEFINE(normal_op_mutex);

/* Semaphores (equivalent to FreeRTOS binary + counting semaphores) */
K_SEM_DEFINE(binary_sem, 0, 1);          /* binary semaphore, starts empty */
K_SEM_DEFINE(counting_sem, 0, 5);        /* counting semaphore, max 5 */
K_SEM_DEFINE(blink_sem, 1, 1);           /* blink LED guard */

/* Demo heap */
K_HEAP_DEFINE(demo_heap, 2048);

static void register_viewalyzer_sync_objects(void)
{
	va_logQueueObjectCreateWithType(&data_msgq, "data_msgq_Queue");
	va_logQueueObjectCreateWithType(&command_msgq, "command_msgq_Queue");

	va_logQueueObjectCreateWithType(&shared_resource_mutex, "shared_resource_mutex_Mutex");
	va_logQueueObjectCreateWithType(&print_mutex, "print_mutex_Mutex");
	va_logQueueObjectCreateWithType(&contention_test_mutex, "contention_test_mutex_Mutex");
	va_logQueueObjectCreateWithType(&normal_op_mutex, "normal_op_mutex_Mutex");

	va_logQueueObjectCreateWithType(&binary_sem, "binary_sem_BinSem");
	va_logQueueObjectCreateWithType(&counting_sem, "counting_sem_CountSem");
	va_logQueueObjectCreateWithType(&blink_sem, "blink_sem_BinSem");

	va_logQueueObjectCreateWithType(&demo_heap, "demo_heap_Heap");
}

/* ── Demo timer ─────────────────────────────────────────────── */
static void heartbeat_timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(heartbeat_timer, heartbeat_timer_handler, NULL);
static volatile uint32_t heartbeatCount;

static void heartbeat_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	heartbeatCount++;
	VA_LogToggle(44, (heartbeatCount & 1) ? TOGGLE_HIGH : TOGGLE_LOW);
}

/* Shared resources protected by mutex */
static volatile uint32_t sharedCounter;
static volatile float    sharedAccumulator;

/* Contention test variables */
static volatile uint32_t contentionCounter;
static volatile uint32_t highPrioAccess;
static volatile uint32_t medPrioAccess;
static volatile uint32_t lowPrioAccess;

/* Normal mutex shared variable */
static volatile uint32_t normalSharedValue;

/* Workload profiles (same as FreeRTOS example) */
#define NUM_TASKS_TO_MANAGE 4
static volatile uint32_t task_workloads[NUM_TASKS_TO_MANAGE] = {1000, 1000, 1000, 1000};

static const uint32_t workload_profiles[][NUM_TASKS_TO_MANAGE] = {
	{4000, 1000, 1000, 6000},
	{1000, 4000, 6000, 1000},
	{6000, 6000, 1000, 1000},
	{1000, 1000, 1000, 1000},
	{2000, 5000, 3000, 4000},
	{5000, 2000, 4000, 3000},
	{3000, 4000, 2000, 5000},
	{4000, 3000, 5000, 2000},
};
#define NUM_PROFILES 8
static volatile int current_profile;


#if defined(CONFIG_SOC_STM32H503XX)
/* STM32H503RB has only 32 KB SRAM — use reduced but safe stacks. */
#define DEFAULT_STACK  512
#define LARGE_STACK    768
#else
#define DEFAULT_STACK  1024
#define LARGE_STACK    2048
#endif

/* Zephyr priorities: lower number = higher priority (opposite of FreeRTOS) */
#define PRIO_HIGHEST     2
#define PRIO_HIGH        3
#define PRIO_NORMAL      5
#define PRIO_LOW         7


static void blink_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_take(&blink_sem, K_FOREVER);
		gpio_pin_toggle_dt(&led);
		k_sem_give(&blink_sem);
		k_msleep(LED_BLINK_TIME_MS);
	}
}

// K_THREAD_DEFINE(blink_tid, DEFAULT_STACK,
// 		blink_thread, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void default_task(void *p1, void *p2, void *p3)
{
	VA_LogString(1, "System started");

	while (1) {
		/* Release binary semaphore for other tasks */
		k_sem_give(&binary_sem);

		/* Give counting semaphore resources periodically */
		k_sem_give(&counting_sem);
		k_sem_give(&counting_sem);

		/* Send periodic commands to WorkloadManager */
		WorkloadCommand_t cmd = {
			.profileIndex = (k_uptime_get_32() / 5000) % NUM_PROFILES,
			.parameter    = k_uptime_get_32(),
		};
		k_msgq_put(&command_msgq, &cmd, K_NO_WAIT);

		k_msleep(500);
	}
}

K_THREAD_DEFINE(default_tid, DEFAULT_STACK,
		default_task, NULL, NULL, NULL,
		PRIO_HIGH, 0, 0);


static void sensor_task(void *p1, void *p2, void *p3)
{
	while (1) {
		sineVal = get_next_sine_value();
		VA_LogTrace(42, sineVal);

		/* Log sine as float (normalized -1.0 to 1.0) */
		float radians = (sine_index * 2.0f * (float)M_PI) / 360.0f;
		VA_LogTraceFloat(60, sinf(radians));

		SensorData_t sd = {
			.sensorValue = sineVal,
			.timestamp   = k_uptime_get_32(),
			.taskId      = 2,
		};
		k_msgq_put(&data_msgq, &sd, K_MSEC(10));

		k_msleep(500);
	}
}

K_THREAD_DEFINE(sensor_tid, DEFAULT_STACK,
		sensor_task, NULL, NULL, NULL,
		PRIO_NORMAL, 0, 0);


static void processor_task(void *p1, void *p2, void *p3)
{
	SensorData_t rx;

	while (1) {
		if (k_msgq_get(&data_msgq, &rx, K_MSEC(100)) == 0) {
			/* Process received sensor data under mutex */
			if (k_mutex_lock(&shared_resource_mutex, K_MSEC(10)) == 0) {
				sharedCounter++;
				sharedAccumulator += rx.sensorValue;
				k_mutex_unlock(&shared_resource_mutex);
			}
			VA_LogTrace(46, rx.sensorValue);
		}

		k_msleep(100);
	}
}

// K_THREAD_DEFINE(processor_tid, DEFAULT_STACK,
// 		processor_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


/* Forward-declare thread IDs for notification targets */
// extern const k_tid_t calculator_tid;
// extern const k_tid_t stack_test_tid;
// extern const k_tid_t consumer_tid;

static void notifier_task(void *p1, void *p2, void *p3)
{
	while (1) {
		k_msleep(100);
	}
}

// K_THREAD_DEFINE(notifier_tid, DEFAULT_STACK,
// 		notifier_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void stack_test_task(void *p1, void *p2, void *p3)
{
	static uint32_t seed = 12345;

	while (1) {
		seed = seed * 1103515245 + 12345;
		uint32_t random_val = (seed >> 16) & 0x7FFF;

		/* Use array on stack to consume varying amounts */
#if defined(CONFIG_SOC_STM32H503XX)
		volatile uint32_t stack_consumer[20];
		uint32_t stack_words = 5 + (random_val % 16);
#else
		volatile uint32_t stack_consumer[80];
		uint32_t stack_words = 10 + (random_val % 71);
#endif

		for (uint32_t i = 0; i < stack_words; i++) {
			stack_consumer[i] = i + k_uptime_get_32();
		}

		volatile uint32_t sum = 0;
		for (uint32_t i = 0; i < stack_words; i++) {
			sum += stack_consumer[i];
		}

		if (k_mutex_lock(&shared_resource_mutex, K_MSEC(100)) == 0) {
			sharedCounter += stack_words;
			sharedAccumulator += sum;
			VA_LogTrace(47, sharedCounter);
			VA_LogTraceFloat(61, sharedAccumulator);
			k_mutex_unlock(&shared_resource_mutex);
		}

		/* Dynamic workload */
		volatile uint32_t wl = task_workloads[1];
		for (volatile uint32_t i = 0; i < wl; i++) {
			__NOP();
		}

		VA_LogTrace(43, k_uptime_get_32());
		k_msleep(10);
	}
}

// K_THREAD_DEFINE(stack_test_tid, LARGE_STACK,
// 		stack_test_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void consumer_task(void *p1, void *p2, void *p3)
{
	while (1) {
		/* Wait for binary semaphore from DefaultTask */
		if (k_sem_take(&binary_sem, K_MSEC(1000)) == 0) {
			volatile uint32_t work = 0;
			for (int i = 0; i < 1000; i++) {
				work += i;
			}

			/* Try counting semaphore resource */
			if (k_sem_take(&counting_sem, K_MSEC(10)) == 0) {
				k_msleep(5);
				k_sem_give(&counting_sem);
			}
		}

		k_msleep(10);
	}
}

// K_THREAD_DEFINE(consumer_tid, DEFAULT_STACK,
// 		consumer_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void worker_task(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_mutex_lock(&print_mutex, K_MSEC(100)) == 0) {
			volatile uint32_t protected_op = k_uptime_get_32() * 2;
			VA_LogTrace(48, protected_op);
			k_mutex_unlock(&print_mutex);
		}

		/* Dynamic workload */
		volatile uint32_t wl = task_workloads[2];
		for (volatile uint32_t i = 0; i < wl; i++) {
			__NOP();
		}

		k_msleep(100);
	}
}

// K_THREAD_DEFINE(worker_tid, DEFAULT_STACK,
// 		worker_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void calculator_task(void *p1, void *p2, void *p3)
{
	while (1) {
		// VA_LogToggle(44, TOGGLE_HIGH);  /* moved to timer handler for debug */
		VA_LogEvent(45, USER_EVENT_START);

		uint16_t invertedSineVal = 200 - sineVal;
		VA_LogTraceFloat(63, (float)invertedSineVal / 100.0f - 1.0f);

		if (k_mutex_lock(&shared_resource_mutex, K_MSEC(50)) == 0) {
			volatile uint32_t localCounter = sharedCounter;
			VA_LogTrace(49, localCounter);
			k_mutex_unlock(&shared_resource_mutex);
		}

		volatile uint32_t wastecycles = 0;
		for (volatile uint32_t i = 0; i < 1000; i++) {
			wastecycles += i;
		}

		/* Dynamic workload */
		volatile uint32_t wl = task_workloads[3];
		for (volatile uint32_t i = 0; i < wl; i++) {
			__NOP();
		}

		// VA_LogToggle(44, TOGGLE_LOW);  /* moved to timer handler for debug */
		VA_LogEvent(45, USER_EVENT_END);

		k_msleep(10);
	}
}

// K_THREAD_DEFINE(calculator_tid, DEFAULT_STACK,
// 		calculator_task, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


static void workload_manager_task(void *p1, void *p2, void *p3)
{
	WorkloadCommand_t rxcmd;
	int64_t lastChange = k_uptime_get();

	while (1) {
		if (k_msgq_get(&command_msgq, &rxcmd, K_MSEC(100)) == 0) {
			if (rxcmd.profileIndex < NUM_PROFILES
			    && rxcmd.profileIndex != current_profile) {
				current_profile = rxcmd.profileIndex;
				for (int i = 0; i < NUM_TASKS_TO_MANAGE; i++) {
					task_workloads[i] = workload_profiles[current_profile][i];
				}
				 VA_LogTrace(50, current_profile);
				lastChange = k_uptime_get();

				/* Cycle the heartbeat timer with new profile period */
				k_timer_stop(&heartbeat_timer);
				k_timer_start(&heartbeat_timer,
					      K_MSEC(250 + current_profile * 100),
					      K_MSEC(250 + current_profile * 100));
			}
		}

		/* Auto profile change every 2.5 s */
		if ((k_uptime_get() - lastChange) > 2500) {
			current_profile = (current_profile + 1) % NUM_PROFILES;
			for (int i = 0; i < NUM_TASKS_TO_MANAGE; i++) {
				task_workloads[i] = workload_profiles[current_profile][i];
			}
			 VA_LogTrace(50, current_profile);
			lastChange = k_uptime_get();

			/* Cycle the heartbeat timer: stop then restart with varied period */
			k_timer_stop(&heartbeat_timer);
			k_timer_start(&heartbeat_timer,
				      K_MSEC(250 + current_profile * 100),
				      K_MSEC(250 + current_profile * 100));
		}

		k_msleep(100);
	}
}

// K_THREAD_DEFINE(wlmgr_tid, DEFAULT_STACK,
// 		workload_manager_task, NULL, NULL, NULL,
// 		PRIO_HIGHEST, 0, 0);


static void contention_low_task(void *p1, void *p2, void *p3)
{
	while (1) {
		/* Low priority holds mutex for a long time → causes contention */
		if (k_mutex_lock(&contention_test_mutex, K_FOREVER) == 0) {
			lowPrioAccess++;
			contentionCounter++;
			VA_LogTrace(51, (int32_t)lowPrioAccess);

			volatile uint32_t work = 0;
			for (int i = 0; i < 50000; i++) {
				work += i;
			}

			/* Hold mutex for 50 ms to ensure contention */
			k_msleep(50);

			k_mutex_unlock(&contention_test_mutex);
		}

		k_msleep(200);
	}
}

static void contention_med_task(void *p1, void *p2, void *p3)
{
	k_msleep(25);  /* offset start */

	while (1) {
		if (k_mutex_lock(&contention_test_mutex, K_MSEC(100)) == 0) {
			medPrioAccess++;
			contentionCounter++;
			VA_LogTrace(52, (int32_t)medPrioAccess);

			volatile uint32_t work = 0;
			for (int i = 0; i < 10000; i++) {
				work += i;
			}
			k_msleep(10);

			k_mutex_unlock(&contention_test_mutex);
		} else {
			VA_LogTrace(52, -1);  /* timeout */
		}

		k_msleep(150);
	}
}

static void contention_high_task(void *p1, void *p2, void *p3)
{
	k_msleep(40);  /* offset start */

	while (1) {
		int64_t start = k_uptime_get();

		if (k_mutex_lock(&contention_test_mutex, K_MSEC(100)) == 0) {
			int32_t waitTime = (int32_t)(k_uptime_get() - start);
			highPrioAccess++;
			contentionCounter++;
			VA_LogTrace(53, SystemCoreClock);
			VA_LogTraceFloat(62, (float)waitTime);

			volatile uint32_t work = 0;
			for (int i = 0; i < 5000; i++) {
				work += i;
			}
			k_msleep(5);

			k_mutex_unlock(&contention_test_mutex);
		} else {
			VA_LogTrace(53, SystemCoreClock);
		}

		k_msleep(120);
	}
}

// K_THREAD_DEFINE(cont_low_tid, DEFAULT_STACK,
// 		contention_low_task, NULL, NULL, NULL,
// 		PRIO_LOW, 0, 0);

// K_THREAD_DEFINE(cont_med_tid, DEFAULT_STACK,
// 		contention_med_task, NULL, NULL, NULL,
// 		PRIO_HIGH, 0, 0);

// K_THREAD_DEFINE(cont_high_tid, DEFAULT_STACK,
// 		contention_high_task, NULL, NULL, NULL,
// 		PRIO_HIGHEST, 0, 0);


static void normal_low_task(void *p1, void *p2, void *p3)
{
	while (1) {
		if (k_mutex_lock(&normal_op_mutex, K_MSEC(50)) == 0) {
			normalSharedValue += 1;
			VA_LogTrace(54, (int32_t)normalSharedValue);
			k_mutex_unlock(&normal_op_mutex);
		}

		volatile uint32_t work = 0;
		for (int i = 0; i < 20000; i++) {
			work += i;
		}

		k_msleep(200);
	}
}

static void normal_med_task(void *p1, void *p2, void *p3)
{
	k_msleep(25);

	while (1) {
		if (k_mutex_lock(&normal_op_mutex, K_MSEC(50)) == 0) {
			normalSharedValue += 10;
			VA_LogTrace(55, (int32_t)normalSharedValue);
			k_mutex_unlock(&normal_op_mutex);
		}

		volatile uint32_t work = 0;
		for (int i = 0; i < 10000; i++) {
			work += i;
		}

		k_msleep(150);
	}
}

static void normal_high_task(void *p1, void *p2, void *p3)
{
	k_msleep(40);

	while (1) {
		if (k_mutex_lock(&normal_op_mutex, K_MSEC(50)) == 0) {
			normalSharedValue += 100;
			VA_LogTrace(56, (int32_t)normalSharedValue);
			k_mutex_unlock(&normal_op_mutex);
		}

		volatile uint32_t work = 0;
		for (int i = 0; i < 5000; i++) {
			work += i;
		}
		tick_counter++;	
		k_msleep(120);
	}
}

// K_THREAD_DEFINE(norm_low_tid, DEFAULT_STACK,
// 		normal_low_task, NULL, NULL, NULL,
// 		PRIO_LOW, 0, 0);

// K_THREAD_DEFINE(norm_med_tid, DEFAULT_STACK,
// 		normal_med_task, NULL, NULL, NULL,
// 		PRIO_HIGH, 0, 0);

// K_THREAD_DEFINE(norm_high_tid, DEFAULT_STACK,
// 		normal_high_task, NULL, NULL, NULL,
// 		PRIO_HIGHEST, 0, 0);


/* ── Heap demo thread ───────────────────────────────────────── */
static void heap_demo_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	/* Cycle through a few allocation sizes so the event table
	   shows varying alloc amounts alongside free events. */
	static const size_t sizes[] = { 64, 128, 256, 32, 512 };
	int idx = 0;

	while (1) {
		size_t sz = sizes[idx % ARRAY_SIZE(sizes)];
		void *ptr = k_heap_alloc(&demo_heap, sz, K_NO_WAIT);
		if (ptr != NULL) {
			/* Simulate brief use of the buffer */
			memset(ptr, 0xAA, sz);
			k_msleep(80);
			k_heap_free(&demo_heap, ptr);
		}
		idx++;
		k_msleep(300);
	}
}

// K_THREAD_DEFINE(heap_demo_tid, DEFAULT_STACK,
// 		heap_demo_thread, NULL, NULL, NULL,
// 		PRIO_NORMAL, 0, 0);


int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Failed to configure LED pin\n");
		return -1;
	}


	k_msleep(1000);                     /* give debugger time to attach */

	VA_Init(SystemCoreClock);

	/* Heartbeat timer disabled for PM testing */
	// k_timer_start(&heartbeat_timer, K_MSEC(500), K_MSEC(500));

	// No need to register is using a schema
	VA_RegisterUserTrace(42, "Sine Value", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(43, "Tick Counter", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(44, "Calc Toggle", VA_USER_TYPE_TOGGLE);
	VA_RegisterUserTrace(46, "Processed Data", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(47, "Shared Counter", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(48, "Protected Op", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(49, "Calc Shared", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(50, "Workload Profile", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(51, "Low Prio Access", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(52, "Med Prio Access", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(53, "High Prio Wait", VA_USER_TYPE_COUNTER);
	VA_RegisterUserTrace(54, "Normal Low", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(55, "Normal Med", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(56, "Normal High", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(60, "Sine Float", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(61, "Shared Accum", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(62, "HighPrio WaitF", VA_USER_TYPE_GRAPH);
	VA_RegisterUserTrace(63, "Inv Sine Float", VA_USER_TYPE_GRAPH);

	VA_RegisterUserEvent(45, "Calc Event");




	return 0;
}
