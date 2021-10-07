/*TO DO:
 * Códigos de flexCAN transfer y MQTT
 * Envío al broker del KeepAlive y el nivel de batería cada 5 y 30s
 * FlexCAN:
 * 	-Envía:
 * 		*KA cada 5s
 * 		*Nivel de batería cada (5, 10 o 15)s
 *	-Recibe:
 *		*Frecuencia del reporte de batería (por evento)
 *		*ON/OFF LED (por evento)
 * ADC:
 * 	-Lee el valor cada n s (definido por el Broker)
 *
 * GPIO:
 * 	-Encender o apagar el led de acuerdo a la señal recibida
*/
/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"
#include "peripherals.h"
#include "fsl_flexcan.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
/* TODO: insert other include files here. */
#include "FreeRTOS.h"
#include "task.h"
#include "MensajesTxRx.h"
#include "ADC16.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define EXAMPLE_CAN            CAN0
#define EXAMPLE_CAN_CLK_SOURCE (kFLEXCAN_ClkSrc1)
#define EXAMPLE_CAN_CLK_FREQ   CLOCK_GetFreq(kCLOCK_BusClk)
/* Set USE_IMPROVED_TIMING_CONFIG macro to use api to calculates the improved CAN / CAN FD timing values. */
#define USE_IMPROVED_TIMING_CONFIG (0U)
#define RX_MESSAGE_BUFFER_NUM      (9)
#define TX_MESSAGE_BUFFER_NUM      (8)
#define DLC                        (8)
#define KA_DELAY				   (5)
#define ACTUATOR_MASK			   (0x01)

/* Fix MISRA_C-2012 Rule 17.7. */
#define LOG_INFO (void)PRINTF
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void CAN_Node_Init(void* Args);
void CAN_Rx_Task(void* Args);
void CAN_Tx_Task(void* Args);
void CAN_Tx_ADC_Task(void* Args);
/*******************************************************************************
 * Variables
 ******************************************************************************/
volatile bool txComplete = false;
volatile bool rxComplete = false;
flexcan_handle_t flexcanHandle;
flexcan_mb_transfer_t txXfer, rxXfer;
flexcan_frame_t txFrame, rxFrame;
flexcan_rx_mb_config_t mbConfig;
uint8_t g_delay_samples = 1;


/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief FlexCAN Call Back function
 */
static FLEXCAN_CALLBACK(flexcan_callback)
{
    switch (status)
    {
        /* Process FlexCAN Rx event. */
        case kStatus_FLEXCAN_RxIdle:
            if (RX_MESSAGE_BUFFER_NUM == result)
            {
                rxComplete = true;
            }
            break;

        /* Process FlexCAN Tx event. */
        case kStatus_FLEXCAN_TxIdle:
            if (TX_MESSAGE_BUFFER_NUM == result)
            {
                txComplete = true;
            }
            break;

        default:
            break;
    }
}

/*!
 * @brief Main function
 */
int main(void)
{
    /* Initialize board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    ADC16_init();
    CAN_Node_Init(NULL);
    /* Start the scheduler. */
    vTaskStartScheduler();

    while(1){
    	// Never get here
    }
}

void CAN_Node_Init(void* Args)
{
	flexcan_config_t flexcanConfig;
	gpio_pin_config_t led_config =
	{
	    kGPIO_DigitalOutput,
		1
	};
	//Configure GPIO for LED
	CLOCK_EnableClock(kCLOCK_PortA);
	PORT_SetPinMux(BOARD_LED_BLUE_PORT, BOARD_LED_BLUE_PIN, kPORT_MuxAsGpio);
	GPIO_PinInit(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_PIN, &led_config);
	//Create CAN tasks
    if(xTaskCreate(CAN_Rx_Task, "CAN_Rx_Task", (configMINIMAL_STACK_SIZE),NULL,(configMAX_PRIORITIES-5),NULL) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Rx_Task");
    	while(1);
     }

    if(xTaskCreate(CAN_Tx_Task,"CAN_Rx_Task",(configMINIMAL_STACK_SIZE),NULL,(configMAX_PRIORITIES-4),NULL) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Tx_Task");
    	while(1);
     }
    //Default configuration
    FLEXCAN_GetDefaultConfig(&flexcanConfig);

#if defined(EXAMPLE_CAN_CLK_SOURCE)
    flexcanConfig.clkSrc = EXAMPLE_CAN_CLK_SOURCE;
#endif

    //flexcanConfig.enableLoopBack = true;
    flexcanConfig.baudRate               = 125000U;

    FLEXCAN_Init(EXAMPLE_CAN, &flexcanConfig, EXAMPLE_CAN_CLK_FREQ);

    /* Create FlexCAN handle structure and set call back function. */
    FLEXCAN_TransferCreateHandle(EXAMPLE_CAN, &flexcanHandle, flexcan_callback, NULL);
}

void CAN_Rx_Task(void* Args){
	uint8_t actuator_contr;
	uint32_t lvl_freq;
	static uint8_t last_status_led = 0;
	/* Setup Rx Message Buffer. */
    mbConfig.format = kFLEXCAN_FrameFormatStandard;
    mbConfig.type   = kFLEXCAN_FrameTypeData;
    mbConfig.id     = FLEXCAN_ID_STD(0x124);	// 0x124 no loop

    FLEXCAN_SetRxMbConfig(EXAMPLE_CAN, RX_MESSAGE_BUFFER_NUM, &mbConfig, true);

    /* Start receive data through Rx Message Buffer. */
    rxXfer.mbIdx = (uint8_t)RX_MESSAGE_BUFFER_NUM;
    rxXfer.frame = &rxFrame;

    for(;;){
    	(void)FLEXCAN_TransferReceiveNonBlocking(EXAMPLE_CAN, &flexcanHandle, &rxXfer);
    	if(rxComplete){
    		//Only for debug

        	LOG_INFO("\r\nReceived message from MB%d\r\n", RX_MESSAGE_BUFFER_NUM);
        	LOG_INFO("rx word0 = 0x%x\r\n", rxFrame.dataWord0);
        	LOG_INFO("rx word1 = %u\r\n", rxFrame.dataWord1);

        	//Only if the received data is the frequency
    		lvl_freq = rxFrame.dataWord0;
    		actuator_contr = rxFrame.dataWord1 & ACTUATOR_MASK;
        	if(lvl_freq != 0x00)
        	{
        		g_delay_samples = rxFrame.dataWord0 / KA_DELAY;
        	}
        	if(actuator_contr != last_status_led)
        	{
        		GPIO_PortToggle(BOARD_LED_BLUE_GPIO, 1U << BOARD_LED_BLUE_GPIO_PIN);
        		last_status_led = actuator_contr;
        		LOG_INFO("LED CHANGED TO %u\r\n",actuator_contr);
        	}
        	rxComplete = false;
    	}
    }
}

void CAN_Tx_Task(void* Args){
	TickType_t xLastWakeTime;
	const TickType_t xPeriod = pdMS_TO_TICKS( KA_DELAY*1000 );	// Keep Alive every 5 seconds
	xLastWakeTime = xTaskGetTickCount();
	static uint8_t delay_counter = 0;

	/* Setup Tx Message Buffer. */
	FLEXCAN_SetTxMbConfig(EXAMPLE_CAN, TX_MESSAGE_BUFFER_NUM, true);

	/* Prepare Tx Frame for sending. */
	txFrame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
	txFrame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
	txFrame.id     = FLEXCAN_ID_STD(0x123);
	txFrame.length = (uint8_t)DLC;

	txXfer.mbIdx = (uint8_t)TX_MESSAGE_BUFFER_NUM;
	txXfer.frame = &txFrame;

	for(;;){
		txFrame.dataWord0 = CAN_WORD0_DATA_BYTE_0(0x00) | CAN_WORD0_DATA_BYTE_1(0x00) | CAN_WORD0_DATA_BYTE_2(0x00) |
				CAN_WORD0_DATA_BYTE_3(KeepAlive_Value);
		//Change dataWord1 depending on the sample
		if(delay_counter == 0)
		{
			txFrame.dataWord1 = ADC16_read();
		}
		else
		{
			txFrame.dataWord1 = CAN_WORD0_DATA_BYTE_0(0x000) | CAN_WORD0_DATA_BYTE_1(0x00) | CAN_WORD0_DATA_BYTE_2(0x00) |
					CAN_WORD0_DATA_BYTE_3(0x00);
		}
		delay_counter++;
		if(delay_counter == g_delay_samples)
		{
			delay_counter = 0;
		}
		/* Send data through Tx Message Buffer. */
		(void)FLEXCAN_TransferSendNonBlocking(EXAMPLE_CAN, &flexcanHandle, &txXfer);
		if(txComplete){
			//Only for debug

			LOG_INFO("\r\nSending KA\r\nSend message from MB%d to MB%d\r\n", TX_MESSAGE_BUFFER_NUM, RX_MESSAGE_BUFFER_NUM);
			LOG_INFO("tx word0 = 0x%x\r\n", txFrame.dataWord0);
			LOG_INFO("tx word1 = %u\r\n", txFrame.dataWord1);

			txComplete = false;
		}else{}
		vTaskDelayUntil( &xLastWakeTime, xPeriod );
	}
}
