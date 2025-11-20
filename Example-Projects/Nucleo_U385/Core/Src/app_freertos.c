/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications - MODIFIED FOR TIMING TEST
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
// Remove CMSIS-OS to use native FreeRTOS APIs
// #include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"      // For printf
#include "ViewAlyzer.h" // For profiler functions
#include <math.h>
#include <stdlib.h> // For rand()

// Ensure DWT cycle counter is enabled *before* scheduler starts!
// Typically in main() before MX_FREERTOS_Init():
// CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
// DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
// DWT->CYCCNT = 0; // Optional: Reset counter

#define GET_TIMESTAMP() (DWT->CYCCNT)
#define TIMESTAMP_FORMAT_STR "%lu" // Format specifier for uint32_t

// Define a workload size. Adjust this based on your CPU speed
// to get a measurable duration (e.g., microseconds to milliseconds).
// Start with a value like 100000 and adjust up or down.
#define WORKLOAD_ITERATIONS 1000

volatile uint16_t sineVal = 0;
volatile uint16_t invertedSineVal = 0;
volatile uint16_t sine_index = 0;
uint16_t get_next_sine_value(void)
{
  // Generate a sine wave value between 0 and 200
  // sine_index goes from 0 to 359 (degrees)
  float radians = (sine_index * 2.0f * (float)M_PI) / 360.0f;
  float sine = sinf(radians);                                   // -1.0 to 1.0
  volatile uint16_t value = (uint16_t)((sine + 1.0f) * 100.0f); // 0 to 200

  sine_index = (sine_index + 1) % 360;
  return value;
}

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
#define NUM_TASKS_TO_MANAGE 4 // Number of tasks whose workload we'll manage
volatile uint32_t task_workloads[NUM_TASKS_TO_MANAGE] = {1000, 1000, 1000, 1000}; // Default workloads

// Define some workload profiles for different phases
const uint32_t workload_profiles[][NUM_TASKS_TO_MANAGE] = {
    {4000, 1000, 1000, 6000}, // Profile 1: Task 2 busy, Task 8 very busy
    {1000, 4000, 6000, 1000}, // Profile 2: Task 5 busy, Task 7 very busy
    {6000, 6000, 1000, 1000}, // Profile 3: Task 2 and 5 busy
    {1000, 1000, 1000, 1000}, // Profile 4: Baseline workloads
    {2000, 5000, 3000, 4000}, // Profile 5: Mixed workloads
    {5000, 2000, 4000, 3000}, // Profile 6: Mixed workloads
    {3000, 4000, 2000, 5000}, // Profile 7: Mixed workloads
    {4000, 3000, 5000, 2000}  // Profile 8: Mixed workloads
};
#define NUM_PROFILES 8
volatile int current_profile = 0;

/* USER CODE END Variables */

// Native FreeRTOS handles - replacing CMSIS-OS types
TaskHandle_t defaultTaskHandle = NULL;
TaskHandle_t myTask02Handle = NULL;
TaskHandle_t myTask03Handle = NULL;
TaskHandle_t myTask04Handle = NULL;
TaskHandle_t myTask05Handle = NULL;
TaskHandle_t myTask06Handle = NULL;
TaskHandle_t myTask07Handle = NULL;
TaskHandle_t myTask08Handle = NULL;
TaskHandle_t workloadManagerTaskHandle = NULL;
TaskHandle_t contentionHighPrioTaskHandle = NULL;  // High priority contention test task
TaskHandle_t contentionMedPrioTaskHandle = NULL;   // Medium priority contention test task
TaskHandle_t contentionLowPrioTaskHandle = NULL;   // Low priority contention test task
TaskHandle_t noisySineWaveTaskHandle = NULL;       // Task that outputs sine wave with noise
TaskHandle_t highFreqNoiseTaskHandle = NULL;       // Task that outputs high frequency noise

// Native FreeRTOS synchronization objects
QueueHandle_t dataQueue = NULL;          // For passing data between tasks
QueueHandle_t commandQueue = NULL;       // For sending commands to WorkloadManager
SemaphoreHandle_t binarySemaphore = NULL;       // Binary semaphore for signaling
SemaphoreHandle_t countingSemaphore = NULL;     // Counting semaphore for resource counting
SemaphoreHandle_t sharedResourceMutex = NULL;   // Mutex for protecting shared resources
SemaphoreHandle_t printMutex = NULL;            // Mutex for protecting printf/debug output
SemaphoreHandle_t contentionTestMutex = NULL;   // Mutex specifically for testing contention tracking

// Data structures for queue communication
typedef struct {
    uint32_t sensorValue;
    uint32_t timestamp;
    uint8_t taskId;
} SensorData_t;

typedef struct {
    uint8_t profileIndex;
    uint32_t parameter;
} WorkloadCommand_t;

// Shared resources protected by mutex
volatile uint32_t sharedCounter = 0;
volatile float sharedAccumulator = 0.0f;

// Contention test variables
volatile uint32_t contentionCounter = 0;
volatile uint32_t highPrioAccess = 0;
volatile uint32_t medPrioAccess = 0;
volatile uint32_t lowPrioAccess = 0;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
extern void ITM_Print(const char *ptr); // Assuming you might use this
void StartTimingTestTask(void *argument);
void WorkloadManagerTask(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);
void StartTask05(void *argument);
void StartTask06(void *argument);
void StartTask07(void *argument);
void StartTask08(void *argument);
void ContentionHighPrioTask(void *argument);
void ContentionMedPrioTask(void *argument);
void ContentionLowPrioTask(void *argument);
void NoisySineWaveTask(void *argument);
void HighFreqNoiseTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void)
{
  // If using FreeRTOS Run Time Stats, ensure it's configured,
  // potentially using DWT as well, but separate from ViewAlyzer's direct use.
  // CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  // DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

__weak unsigned long getRunTimeCounterValue(void)
{
  // return DWT->CYCCNT; // If using DWT for run time stats
  return 0;
}
/* USER CODE END 1 */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */
  // --- IMPORTANT ---
  // Ensure DWT Cycle Counter is Enabled HERE or in main() before this function!
  // Example (should be done only once):
  // if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) { // Enable TRCENA
  //     CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  // }
  // if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) { // Enable CYCCNT
  //     DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  // }
  // DWT->CYCCNT = 0; // Optional reset
  // --- End Important ---

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  // Create mutexes using native FreeRTOS API
  // Note: ViewAlyzer will automatically track these via trace macros
  sharedResourceMutex = xSemaphoreCreateMutex();
  printMutex = xSemaphoreCreateMutex();
  contentionTestMutex = xSemaphoreCreateMutex(); // Mutex for contention testing
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  // Create semaphores using native FreeRTOS API
  // Note: ViewAlyzer will automatically track these via trace macros
  binarySemaphore = xSemaphoreCreateBinary();
  countingSemaphore = xSemaphoreCreateCounting(5, 0); // Max 5 resources, start with 0
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* Create queues using native FreeRTOS API */
  // Note: ViewAlyzer will automatically track these via trace macros
  dataQueue = xQueueCreate(10, sizeof(SensorData_t));
  commandQueue = xQueueCreate(5, sizeof(WorkloadCommand_t));

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) using native FreeRTOS API */
  xTaskCreate(StartDefaultTask, "DefaultTask", 128, NULL, tskIDLE_PRIORITY + 2, &defaultTaskHandle);
  xTaskCreate(StartTask02, "SensorTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask02Handle);
  xTaskCreate(StartTask03, "ProcessorTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask03Handle);
  xTaskCreate(StartTask04, "NotifierTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask04Handle);
  xTaskCreate(StartTask05, "StackTestTask", 256, NULL, tskIDLE_PRIORITY + 1, &myTask05Handle); // Larger stack for testing
  xTaskCreate(StartTask06, "ConsumerTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask06Handle);
  xTaskCreate(StartTask07, "WorkerTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask07Handle);
  xTaskCreate(StartTask08, "CalculatorTask", 128, NULL, tskIDLE_PRIORITY + 1, &myTask08Handle);
  xTaskCreate(WorkloadManagerTask, "WorkloadManager", 256, NULL, tskIDLE_PRIORITY + 3, &workloadManagerTaskHandle); // Higher priority
  
  // Contention test tasks with different priorities to showcase mutex contention tracking
  xTaskCreate(ContentionHighPrioTask, "ContentionHigh", 128, NULL, tskIDLE_PRIORITY + 5, &contentionHighPrioTaskHandle); // Highest priority
  xTaskCreate(ContentionMedPrioTask, "ContentionMed", 128, NULL, tskIDLE_PRIORITY + 3, &contentionMedPrioTaskHandle);    // Medium priority
  xTaskCreate(ContentionLowPrioTask, "ContentionLow", 128, NULL, tskIDLE_PRIORITY + 1, &contentionLowPrioTaskHandle);    // Low priority
  
  // Noise generation tasks for testing filters
  xTaskCreate(NoisySineWaveTask, "NoisySineWave", 128, NULL, tskIDLE_PRIORITY + 1, &noisySineWaveTaskHandle);  // Sine wave with noise
  xTaskCreate(HighFreqNoiseTask, "HighFreqNoise", 128, NULL, tskIDLE_PRIORITY + 1, &highFreqNoiseTaskHandle);  // High frequency noise

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  for (;;)
  {
    // Release binary semaphore for other tasks to use
    if (binarySemaphore != NULL)
    {
      xSemaphoreGive(binarySemaphore);
    }
    
    // Give counting semaphore resources periodically
    if (countingSemaphore != NULL)
    {
      xSemaphoreGive(countingSemaphore);
      xSemaphoreGive(countingSemaphore);
    }
    
    // Send periodic commands to WorkloadManager
    if (commandQueue != NULL)
    {
      WorkloadCommand_t cmd = {
        .profileIndex = (HAL_GetTick() / 5000) % NUM_PROFILES, // Change profile every 5 seconds
        .parameter = HAL_GetTick()
      };
      
      // Try to send command (non-blocking)
      if (xQueueSend(commandQueue, &cmd, 0) == pdPASS)
      {
        // Command sent successfully
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
 * @brief Function implementing the myTask02 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  for (;;)
  {
    // Generate sensor data and send to queue
    sineVal = get_next_sine_value(); // Get next sine value
  //  VA_LogTrace(42, sineVal);    // Log the sine value trace
    
    // Create sensor data structure
    SensorData_t sensorData = {
      .sensorValue = sineVal,
      .timestamp = HAL_GetTick(),
      .taskId = 2
    };
    
    // Send data to queue for other tasks to process
    if (dataQueue != NULL)
    {
      if (xQueueSend(dataQueue, &sensorData, pdMS_TO_TICKS(10)) == pdPASS)
      {
        // Data sent successfully
      }
    }

    // Wait for notification from Task04
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    
    // Dynamic workload
    volatile uint32_t workload = task_workloads[0];
    for (volatile uint32_t i = 0; i < workload; i++)
    {
      // Simple calculation to consume CPU time
      __NOP();
    }

    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
 * @brief Function implementing the myTask03 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  SensorData_t receivedData;
  
  for (;;)
  {
    // Wait for data from queue (blocking with timeout)
    if (dataQueue != NULL)
    {
      if (xQueueReceive(dataQueue, &receivedData, pdMS_TO_TICKS(100)) == pdPASS)
      {
        // Process received sensor data
        // Use mutex to protect shared resource access
        if (xSemaphoreTake(sharedResourceMutex, pdMS_TO_TICKS(10)) == pdPASS)
        {
          sharedCounter++;
          sharedAccumulator += receivedData.sensorValue;
          xSemaphoreGive(sharedResourceMutex);
        }
        
        // Log processed data
   //     VA_LogTrace(46, receivedData.sensorValue);
      }
    }
    
    // Also wait for notification from Task 4
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    vTaskDelay(pdMS_TO_TICKS(16));
  }
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
 * @brief Function implementing the myTask04 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  volatile uint32_t ulValue = 0; // Notification value to send
  for (;;)
  {
    // Send notifications to multiple tasks using native FreeRTOS API
    if (myTask08Handle != NULL)
    {
      xTaskNotify(myTask08Handle, ulValue++, eSetValueWithOverwrite);
    }
    if (myTask05Handle != NULL)
    {
      xTaskNotify(myTask05Handle, ulValue++, eSetValueWithOverwrite);
    }
    if (myTask03Handle != NULL)
    {
      xTaskNotify(myTask03Handle, ulValue++, eSetValueWithOverwrite);
    }
    if (myTask02Handle != NULL)
    {
      xTaskNotify(myTask02Handle, ulValue++, eSetValueWithOverwrite);
    }
    if (myTask06Handle != NULL)
    {
      xTaskNotify(myTask06Handle, ulValue++, eSetValueWithOverwrite);
    }
    
    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask04 */
}

/* USER CODE BEGIN Header_StartTask05 */
/**
 * @brief Function implementing the myTask05 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask05 */
void StartTask05(void *argument)
{
  /* USER CODE BEGIN StartTask05 */
  // Seed for simple pseudo-random number generation
  static uint32_t seed = 12345;

  for (;;)
  {
    // Generate a pseudo-random number to vary stack usage
    seed = seed * 1103515245 + 12345;            // Simple LCG
    uint32_t random_val = (seed >> 16) & 0x7FFF; // Get 15-bit value

    // Use random amount of stack (10 to 80 words, leaving safety margin)
    uint32_t stack_words = 10 + (random_val % 71); // 10-80 words

    // Create variable-sized array on stack to consume stack space
    // Note: This is a non-standard extension but works with GCC
    volatile uint32_t stack_consumer[stack_words];
    
    // Wait for notification from Task04
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    
    // Write to the array to ensure it's actually allocated and used
    for (uint32_t i = 0; i < stack_words; i++)
    {
      stack_consumer[i] = i + HAL_GetTick();
    }

    // Do some computation with the array to prevent optimization
    volatile uint32_t sum = 0;
    for (uint32_t i = 0; i < stack_words; i++)
    {
      sum += stack_consumer[i];
    }

    // Use mutex to safely access shared resources
    if (xSemaphoreTake(sharedResourceMutex, pdMS_TO_TICKS(100)) == pdPASS)
    {
      sharedCounter += stack_words;
      sharedAccumulator += sum;
      
      // Log the shared counter value
     // VA_LogTrace(47, sharedCounter);
      
      xSemaphoreGive(sharedResourceMutex);
    }

    // Dynamic workload
    volatile uint32_t workload = task_workloads[1];
    for (volatile uint32_t i = 0; i < workload; i++)
    {
      // Simple calculation to consume CPU time
      __NOP();
    }

    // Log the amount of stack used for debugging
   // VA_LogTrace(43, HAL_GetTick()); // Log tick counter

    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask05 */
}

/* USER CODE BEGIN Header_StartTask06 */
/**
 * @brief Function implementing the myTask06 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask06 */
void StartTask06(void *argument)
{
  /* USER CODE BEGIN StartTask06 */
  for (;;)
  {
    // Wait for notification from Task04
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    
    // Wait for binary semaphore from defaultTask
    if (binarySemaphore != NULL)
    {
      if (xSemaphoreTake(binarySemaphore, pdMS_TO_TICKS(1000)) == pdPASS)
      {
        // Do some work when semaphore is acquired
        // This will create timing events in the profiler
        volatile uint32_t work = 0;
        for (int i = 0; i < 1000; i++)
        {
          work += i;
        }
        
        // Try to take a counting semaphore resource
        if (countingSemaphore != NULL)
        {
          if (xSemaphoreTake(countingSemaphore, pdMS_TO_TICKS(10)) == pdPASS)
          {
            // Use the resource for some time
            vTaskDelay(pdMS_TO_TICKS(5));
            
            // Release the counting semaphore resource
            xSemaphoreGive(countingSemaphore);
          }
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask06 */
}

/* USER CODE BEGIN Header_StartTask07 */
/**
 * @brief Function implementing the myTask07 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask07 */
void StartTask07(void *argument)
{
  /* USER CODE BEGIN StartTask07 */
  for (;;)
  {
    // Use print mutex to safely output debug information
    if (xSemaphoreTake(printMutex, pdMS_TO_TICKS(100)) == pdPASS)
    {
      // Safe to use printf or other print functions here
      // In this case, we'll just do some computation to simulate protected operation
      volatile uint32_t protected_operation = HAL_GetTick() * 2;
   //   VA_LogTrace(48, protected_operation); // Log protected operation result
      
      xSemaphoreGive(printMutex);
    }
    
    // Dynamic workload
    volatile uint32_t workload = task_workloads[2];
    for (volatile uint32_t i = 0; i < workload; i++)
    {
      // Simple calculation to consume CPU time
      __NOP();
    }
    
    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask07 */
}

/* USER CODE BEGIN Header_StartTask08 */
/**
 * @brief Function implementing the myTask08 thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTask08 */
void StartTask08(void *argument)
{
  /* USER CODE BEGIN StartTask08 */
  for (;;)
  {
    // Wait for notification from Task04 (blocking with max delay)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    // Log function entry
    VA_LogToggle(44, TOGGLE_HIGH);
    VA_LogUserEvent(45, USER_EVENT_START);
    
    // Get inverted sine value
    invertedSineVal = 200 - sineVal;
    
    // Access shared resource safely using mutex
    if (xSemaphoreTake(sharedResourceMutex, pdMS_TO_TICKS(50)) == pdPASS)
    {
      // Read shared data safely
      volatile uint32_t localCounter = sharedCounter;
      volatile float localAccumulator = sharedAccumulator;
      
      // Log the shared values
//VA_LogTrace(49, localCounter);
      
      xSemaphoreGive(sharedResourceMutex);
    }
    
    volatile uint32_t wastecycles = 0;
    for (volatile uint32_t i = 0; i < WORKLOAD_ITERATIONS; i++)
    {
      wastecycles += i; // Simple workload to create timing
    }

    // Dynamic workload
    volatile uint32_t workload = task_workloads[3];
    for (volatile uint32_t i = 0; i < workload; i++)
    {
      // Simple calculation to consume CPU time
      __NOP();
    }

    // Log function exit
 //   VA_LogToggle(44, TOGGLE_LOW);
//    VA_LogUserEvent(45, USER_EVENT_END);
    
    vTaskDelay(pdMS_TO_TICKS(16)); // Use native FreeRTOS delay
  }
  /* USER CODE END StartTask08 */
}

/**
 * @brief Function implementing the WorkloadManagerTask thread.
 * @param argument: Not used
 * @retval None
 */
void WorkloadManagerTask(void *argument)
{
  /* USER CODE BEGIN WorkloadManagerTask */
  WorkloadCommand_t receivedCommand;
  TickType_t lastProfileChange = xTaskGetTickCount();
  
  for (;;)
  {
    // Check for commands from command queue (non-blocking)
    if (commandQueue != NULL)
    {
      if (xQueueReceive(commandQueue, &receivedCommand, pdMS_TO_TICKS(100)) == pdPASS)
      {
        // Process received command
        if (receivedCommand.profileIndex < NUM_PROFILES)
        {
          current_profile = receivedCommand.profileIndex;
          
          // Update the workloads for the managed tasks
          for (int i = 0; i < NUM_TASKS_TO_MANAGE; i++)
          {
            task_workloads[i] = workload_profiles[current_profile][i];
          }
          
       //   VA_LogTrace(50, current_profile); // Log the current profile index
          lastProfileChange = xTaskGetTickCount();
        }
      }
    }
    
    // Automatic profile change every 2.5 seconds if no commands received
    if ((xTaskGetTickCount() - lastProfileChange) > pdMS_TO_TICKS(2500))
    {
      current_profile = rand() % NUM_PROFILES; // Pick a random profile

      // Update the workloads for the managed tasks
      for (int i = 0; i < NUM_TASKS_TO_MANAGE; i++)
      {
        task_workloads[i] = workload_profiles[current_profile][i];
      }
      
   //   VA_LogTrace(50, current_profile); // Log the current profile index
      lastProfileChange = xTaskGetTickCount();
    }
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
  }
  /* USER CODE END WorkloadManagerTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE BEGIN Header_ContentionLowPrioTask */
/**
 * @brief Low priority task that holds mutex for extended periods
 *        This will cause contention when higher priority tasks try to acquire
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_ContentionLowPrioTask */
void ContentionLowPrioTask(void *argument)
{
  /* USER CODE BEGIN ContentionLowPrioTask */
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  for (;;)
  {
    // This low priority task acquires the mutex and holds it for a while
    if (xSemaphoreTake(contentionTestMutex, portMAX_DELAY) == pdPASS)
    {
      // Simulate critical section work - hold mutex for 50ms
      // This will cause high-priority tasks to block and generate contention events
      lowPrioAccess++;
      contentionCounter++;
      
      // Log that low priority task has the mutex
   //   VA_LogTrace(51, (int32_t)lowPrioAccess);
      
      // Do some "important" work while holding the mutex
      volatile uint32_t work = 0;
      for (int i = 0; i < 50000; i++)
      {
        work += i;
      }
      
      // Hold the mutex for 50ms to ensure contention
      vTaskDelay(pdMS_TO_TICKS(50));
      
      xSemaphoreGive(contentionTestMutex);
    }
    
    // Run every 200ms
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
  }
  /* USER CODE END ContentionLowPrioTask */
}

/* USER CODE BEGIN Header_ContentionMedPrioTask */
/**
 * @brief Medium priority task that competes for the mutex
 *        Will experience contention when low-priority task holds mutex
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_ContentionMedPrioTask */
void ContentionMedPrioTask(void *argument)
{
  /* USER CODE BEGIN ContentionMedPrioTask */
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  // Offset start time to create contention
  vTaskDelay(pdMS_TO_TICKS(25));
  
  for (;;)
  {
    // Try to acquire mutex - will block if low priority task has it
    if (xSemaphoreTake(contentionTestMutex, pdMS_TO_TICKS(100)) == pdPASS)
    {
      medPrioAccess++;
      contentionCounter++;
      
      // Log that medium priority task has the mutex
  //    VA_LogTrace(52, (int32_t)medPrioAccess);
      
      // Do some quick work while holding the mutex (10ms)
      volatile uint32_t work = 0;
      for (int i = 0; i < 10000; i++)
      {
        work += i;
      }
      
      vTaskDelay(pdMS_TO_TICKS(10));
      
      xSemaphoreGive(contentionTestMutex);
    }
    else
    {
      // Timeout waiting for mutex - log the failed attempt
   //   VA_LogTrace(52, -1); // Negative value indicates timeout
    }
    
    // Run every 150ms
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(150));
  }
  /* USER CODE END ContentionMedPrioTask */
}

/* USER CODE BEGIN Header_ContentionHighPrioTask */
/**
 * @brief High priority task that urgently needs the mutex
 *        This will showcase priority inheritance when blocked by low-priority task
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_ContentionHighPrioTask */
void ContentionHighPrioTask(void *argument)
{
  /* USER CODE BEGIN ContentionHighPrioTask */
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  // Offset start time to maximize contention scenarios
  vTaskDelay(pdMS_TO_TICKS(40));
  
  for (;;)
  {
    // High priority task tries to acquire mutex urgently
    // When blocked by low-priority task, priority inheritance should kick in
    TickType_t startTime = xTaskGetTickCount();
    
    if (xSemaphoreTake(contentionTestMutex, pdMS_TO_TICKS(100)) == pdPASS)
    {
      TickType_t waitTime = xTaskGetTickCount() - startTime;
      highPrioAccess++;
      contentionCounter++;
      
      // Log that high priority task has the mutex
      // Use wait time as value to see how long we blocked
   //   VA_LogTrace(53, (int32_t)waitTime);
      
      // High priority task does critical work quickly (5ms)
      volatile uint32_t work = 0;
      for (int i = 0; i < 5000; i++)
      {
        work += i;
      }
      
      vTaskDelay(pdMS_TO_TICKS(5));
      
      xSemaphoreGive(contentionTestMutex);
    }
    else
    {
      // Timeout - this is bad for high priority task!
  //    VA_LogTrace(53, -999); // Large negative value indicates critical timeout
    }
   // HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin); // Toggle LED to visualize activity
    // Run every 120ms (creates interesting contention patterns with other tasks)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(120));
  }
  /* USER CODE END ContentionHighPrioTask */
}

/* USER CODE BEGIN Header_NoisySineWaveTask */
/**
 * @brief Task that generates a sine wave with added random noise
 *        Useful for testing low-pass filters and noise reduction on the frontend
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_NoisySineWaveTask */
void NoisySineWaveTask(void *argument)
{
  /* USER CODE BEGIN NoisySineWaveTask */
  uint16_t local_sine_index = 0;
  
  for (;;)
  {
    // Generate clean sine wave value
    float radians = (local_sine_index * 2.0f * (float)M_PI) / 360.0f;
    float sine = sinf(radians);                      // -1.0 to 1.0
    float clean_value = (sine + 1.0f) * 100.0f;      // 0 to 200
    
    // Add random noise (±20% of full scale, which is ±40 units)
    int32_t noise = (rand() % 81) - 40;  // -40 to +40
    int32_t noisy_value = (int32_t)clean_value + noise;
    
    // Clamp to valid range
    if (noisy_value < 0) noisy_value = 0;
    if (noisy_value > 200) noisy_value = 200;
    
    // Log the noisy sine wave value (trace ID 60)
    VA_LogTrace(60, (int32_t)noisy_value);
    
    local_sine_index = (local_sine_index + 1) % 360;
    
    vTaskDelay(pdMS_TO_TICKS(16)); // 16ms = ~60Hz update rate
  }
  /* USER CODE END NoisySineWaveTask */
}

/* USER CODE BEGIN Header_HighFreqNoiseTask */
/**
 * @brief Task that generates high-frequency random noise
 *        Useful for testing filters and visualizing noise characteristics
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_HighFreqNoiseTask */
void HighFreqNoiseTask(void *argument)
{
  /* USER CODE BEGIN HighFreqNoiseTask */
  for (;;)
  {
    // Generate completely random high-frequency noise
    // Value range: 0 to 200 (same as other signals for easy comparison)
    int32_t noise_value = rand() % 201;  // 0 to 200
    
    // Log the noise value (trace ID 61)
    VA_LogTrace(61, noise_value);
    
    // Update at high frequency (4ms = 250Hz) to create high-freq noise
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  /* USER CODE END HighFreqNoiseTask */
}

/* USER CODE END Application */
