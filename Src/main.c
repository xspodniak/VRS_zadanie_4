/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);


/* Function processing DMA Rx data. Counts how many capital and small letters are in sentence.
 * Result is supposed to be stored in global variable of type "letter_count_" that is defined in "main.h"
 *
 * @param1 - received sign
 */
void proccesDmaData(uint8_t sign);


/* Space for your global variables. */

#define MAX_STRING_LEN 35		// max. number of chars after '#' (incl. '$')

/* 1 ms tick, incremented in SysTick_Handler */
volatile uint32_t tick_ms = 0;

/* Result of last valid string - filled in proccesDmaData (interrupt context) */
volatile letter_count_ letter_count = {0, 0};
volatile uint8_t valid_string_ready = 0;
char valid_string[MAX_STRING_LEN + 1];

/* Internal state of the parser */
static uint8_t receiving = 0;
static uint8_t rx_len = 0;
static char rx_string[MAX_STRING_LEN + 1];

/* Tx buffer for USART2 DMA */
static char tx_buffer[128];

/* Send string over USART2 DMA, waits until previous transmission ends */
static void sendString(const char *str)
{
	while(LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_7));
	strncpy(tx_buffer, str, sizeof(tx_buffer) - 1);
	tx_buffer[sizeof(tx_buffer) - 1] = '\0';
	USART2_PutBuffer((uint8_t *)tx_buffer, strlen(tx_buffer));
}


int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
  NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

  /* Configure the system clock */
  SystemClock_Config();
  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();

  /* Space for your local variables, callback registration ...*/

  LL_SYSTICK_EnableIT();
  USART2_RegisterCallback(proccesDmaData);

  char msg[128];
  uint32_t last_report = tick_ms;

  while (1)
  {
	  /* Periodic transmission of information about DMA Rx buffer state.
	   * Transmission frequency - 0.5Hz.
	   * Message format - "Buffer capacity: %d bytes, occupied memory: %d bytes, load [in %]: %f%"
	   * Example message (what I wish to see in terminal) - Buffer capacity: 1000 bytes, occupied memory: 231 bytes, load [in %]: 23.1%
	   */

	  /* Valid text string information transmission.
	   * Transmission frequency - when new valid string is received.
	   * Message format - "Valid string: %s, lower-case: %d, upper-case: %d"
	   * Example message (what I wish to see in terminal) - Valid string: Platn15uborZnakov, lower-case: 13, upper-case: 2
	   */

	  if((tick_ms - last_report) >= 2000)	// 0.5 Hz
	  {
		  last_report += 2000;

		  uint16_t occupied = USART2_GetDmaRxOccupied();
		  uint32_t load_x10 = ((uint32_t)occupied * 1000) / DMA_USART2_BUFFER_SIZE;	// load in 0.1 %

		  sprintf(msg, "Buffer capacity: %d bytes, occupied memory: %d bytes, load [in %%]: %lu.%lu%%\r\n",
				  DMA_USART2_BUFFER_SIZE, occupied, load_x10 / 10, load_x10 % 10);
		  sendString(msg);
	  }

	  if(valid_string_ready)
	  {
		  __disable_irq();
		  sprintf(msg, "Valid string: %s, lower-case: %d, upper-case: %d\r\n",
				  valid_string, letter_count.small_letter, letter_count.capital_letter);
		  valid_string_ready = 0;
		  __enable_irq();
		  sendString(msg);
	  }
  }
  /* USER CODE END 3 */
}


void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_0);

  if(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_0)
  {
  Error_Handler();  
  }
  LL_RCC_HSI_Enable();

   /* Wait till HSI is ready */
  while(LL_RCC_HSI_IsReady() != 1)
  {
    
  }
  LL_RCC_HSI_SetCalibTrimming(16);
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
  
  }
  LL_Init1msTick(8000000);
  LL_SYSTICK_SetClkSource(LL_SYSTICK_CLKSOURCE_HCLK);
  LL_SetSystemCoreClock(8000000);
}

/*
 * Implementation of function processing data received via USART.
 */
void proccesDmaData(uint8_t sign)
{
	/* Process received data */

	if(sign == '#')
	{
		/* Start of new string (also restarts unfinished one) */
		receiving = 1;
		rx_len = 0;
		return;
	}

	if(!receiving) return;		// ignore everything until '#'

	if(sign == '$')
	{
		/* End of valid string -> evaluate it */
		uint8_t small = 0, capital = 0;

		rx_string[rx_len] = '\0';
		for(uint8_t i = 0; i < rx_len; i++)
		{
			if(rx_string[i] >= 'a' && rx_string[i] <= 'z') small++;
			else if(rx_string[i] >= 'A' && rx_string[i] <= 'Z') capital++;
		}

		strcpy(valid_string, rx_string);
		letter_count.small_letter = small;
		letter_count.capital_letter = capital;
		valid_string_ready = 1;

		receiving = 0;
		return;
	}

	if(rx_len >= MAX_STRING_LEN - 1)
	{
		/* '$' not received within 35 characters -> discard data */
		receiving = 0;
		rx_len = 0;
		return;
	}

	rx_string[rx_len++] = sign;
}


void Error_Handler(void)
{

}

#ifdef  USE_FULL_ASSERT

void assert_failed(char *file, uint32_t line)
{ 

}

#endif

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
