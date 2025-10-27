/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ViewAlyzer.h"
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

COM_InitTypeDef BspCOMInit;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void SWO_Init(uint32_t cpu_hz, uint32_t swo_baud, uint32_t port)
{
  // 0) DBG + GPIO
#ifdef DBGMCU_CR_TRACE_IOEN
  DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // enable trace pins
#ifdef DBGMCU_CR_TRACE_MODE
  DBGMCU->CR &= ~DBGMCU_CR_TRACE_MODE; // 00 = async SWO (not sync TRACE)
#endif
#endif

  // 1) Enable trace fabric
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // TRCENA

  // 2) Lock things down while we configure
#ifdef ITM_LAR
  ITM->LAR = 0xC5ACCE55;
#endif
  ITM->TCR = 0; // disable ITM while configuring
  ITM->TPR = 0; // allow all stimulus ports (unprivileged OK)
  ITM->TER = 0; // disable all ports for now

#ifdef DBGMCU_CR_TRACE_IOEN
  DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // enable trace pins
#ifdef DBGMCU_CR_TRACE_MODE
  DBGMCU->CR &= ~DBGMCU_CR_TRACE_MODE; // 00b = async SWO (important!)
#endif
#endif

  // 3) TPIU for async NRZ @ exact baud
  uint32_t acpr = (swo_baud ? (cpu_hz / swo_baud) - 1u : 0u);
  TPI->ACPR = acpr;  // *** use the math; don't hard-code ***
  TPI->SPPR = 0x2;   // 2 = NRZ (UART) ; 1 = Manchester
  TPI->FFCR = 0x100; // disable continuous formatter
  TPI->CSPSR = 1;    // port size = 1 bit (good default)

  // 4) Enable your ITM port and the ITM itself
  ITM->TER = (1u << port);
  ITM->TCR = ITM_TCR_ITMENA_Msk; // *** ITMENA is bit 0 ***
                                 // optional: timestamps, sync:
                                 // ITM->TCR |= ITM_TCR_TSENA_Msk | ITM_TCR_SYNCENA_Msk;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  SWO_Init(170000000u, 2000000u, 1);
  HAL_Delay(3000);
  VA_Init(SystemCoreClock);     
                                                        
  VA_RegisterUserTrace(42, "Sine Wave", VA_USER_TYPE_GRAPH);     // Task02: Sine value from sensor
  VA_RegisterUserTrace(43, "Tick Counter", VA_USER_TYPE_GRAPH);  // Task05: HAL tick counter
  VA_RegisterUserTrace(44, "Task08 Toggle", VA_USER_TYPE_TOGGLE); // Task08: Function entry/exit toggle
  VA_RegisterUserFunction(45, "Custom Function");                // Task08: Function timing
  VA_RegisterUserTrace(46, "Processed Data", VA_USER_TYPE_GRAPH); // Task03: Processed sensor values
  VA_RegisterUserTrace(47, "Shared Counter", VA_USER_TYPE_COUNTER); // Task05: Mutex-protected shared counter
  VA_RegisterUserTrace(48, "Protected Op", VA_USER_TYPE_GRAPH);   // Task07: Mutex-protected operation result
  VA_RegisterUserTrace(49, "Local Counter", VA_USER_TYPE_GRAPH);  // Task08: Local copy of shared counter
  VA_RegisterUserTrace(50, "Workload Profile", VA_USER_TYPE_BAR); // WorkloadManager: Current profile index
  
  // Contention test task traces
  VA_RegisterUserTrace(51, "Low Prio Access", VA_USER_TYPE_COUNTER);  // ContentionLow: How many times accessed mutex
  VA_RegisterUserTrace(52, "Med Prio Access", VA_USER_TYPE_COUNTER);  // ContentionMed: How many times accessed mutex (-1 = timeout)
  VA_RegisterUserTrace(53, "High Prio Wait", VA_USER_TYPE_GRAPH);     // ContentionHigh: Wait time in ticks to get mutex
  
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Initialize led */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
