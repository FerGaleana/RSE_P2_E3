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
#include "fsl_flexcan.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
/* TODO: insert other include files here. */
#include "FreeRTOS.h"
#include "task.h"
#include "MensajesTxRx.h"

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

/* Fix MISRA_C-2012 Rule 17.7. */
#define LOG_INFO (void)PRINTF
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void CAN_Rx_Task(void* Args);
void CAN_Tx_Task(void* Args);

/*******************************************************************************
 * Variables
 ******************************************************************************/
volatile bool txComplete = false;
volatile bool rxComplete = false;
flexcan_handle_t flexcanHandle;
flexcan_mb_transfer_t txXfer, rxXfer;
flexcan_frame_t txFrame, rxFrame;
flexcan_rx_mb_config_t mbConfig;


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
    flexcan_config_t flexcanConfig;

    /* Initialize board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    if(xTaskCreate(
      CAN_Rx_Task, /* Pointer to the function that implements the task. */
      "CAN_Rx_Task", /* Text name given to the task. */
	  (configMINIMAL_STACK_SIZE*10), /* The size of the stack that should be created for the task.
                                        This is defined in words, not bytes. */
      NULL,/* A reference to xParameters is used as the task parameter.
              This is cast to a void * to prevent compiler warnings. */
	  (configMAX_PRIORITIES-4), /* The priority to assign to the newly created task. */
      NULL /* The handle to the task being created will be placed in
              xHandle. */
     ) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Rx_Task");
    	while(1);
     }


    if(xTaskCreate(
      CAN_Tx_Task, /* Pointer to the function that implements the task. */
      "CAN_Rx_Task", /* Text name given to the task. */
	  (configMINIMAL_STACK_SIZE*10), /* The size of the stack that should be created for the task.
                                        This is defined in words, not bytes. */
      NULL,/* A reference to xParameters is used as the task parameter.
              This is cast to a void * to prevent compiler warnings. */
	  (configMAX_PRIORITIES-3), /* The priority to assign to the newly created task. */
      NULL /* The handle to the task being created will be placed in
              xHandle. */
     ) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Tx_Task");
    	while(1);
     }

    LOG_INFO("\r\n==FlexCAN loopback example -- Start.==\r\n\r\n");

    /* Init FlexCAN module. */
    /*
     * flexcanConfig.clkSrc                 = kFLEXCAN_ClkSrc0;
     * flexcanConfig.baudRate               = 1000000U;
     * flexcanConfig.baudRateFD             = 2000000U;
     * flexcanConfig.maxMbNum               = 16;
     * flexcanConfig.enableLoopBack         = false;
     * flexcanConfig.enableSelfWakeup       = false;
     * flexcanConfig.enableIndividMask      = false;
     * flexcanConfig.disableSelfReception   = false;
     * flexcanConfig.enableListenOnlyMode   = false;
     * flexcanConfig.enableDoze             = false;
     */
    FLEXCAN_GetDefaultConfig(&flexcanConfig);

#if defined(EXAMPLE_CAN_CLK_SOURCE)
    flexcanConfig.clkSrc = EXAMPLE_CAN_CLK_SOURCE;
#endif

//    flexcanConfig.enableLoopBack = true;
    flexcanConfig.baudRate               = 125000U;

    FLEXCAN_Init(EXAMPLE_CAN, &flexcanConfig, EXAMPLE_CAN_CLK_FREQ);

    /* Create FlexCAN handle structure and set call back function. */
    FLEXCAN_TransferCreateHandle(EXAMPLE_CAN, &flexcanHandle, flexcan_callback, NULL);

    /* Start the scheduler. */
    vTaskStartScheduler();

    while(1){
    	// Never come here
    }
}


void CAN_Rx_Task(void* Args){
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
        	LOG_INFO("\r\nReceived message from MB%d\r\n", RX_MESSAGE_BUFFER_NUM);
        	LOG_INFO("rx word0 = 0x%x\r\n", rxFrame.dataWord0);
        	LOG_INFO("rx word1 = 0x%x\r\n", rxFrame.dataWord1);
        	rxComplete = false;
    	}
    }
}



void CAN_Tx_Task(void* Args){
	TickType_t xLastWakeTime;
	const TickType_t xPeriod = pdMS_TO_TICKS( 4000 );	// Keep Alive every 5 seconds
	xLastWakeTime = xTaskGetTickCount();

	/* Setup Tx Message Buffer. */
	FLEXCAN_SetTxMbConfig(EXAMPLE_CAN, TX_MESSAGE_BUFFER_NUM, true);

	/* Prepare Tx Frame for sending. */
	txFrame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
	txFrame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
	txFrame.id     = FLEXCAN_ID_STD(0x123);
	txFrame.length = (uint8_t)DLC;

	txXfer.mbIdx = (uint8_t)TX_MESSAGE_BUFFER_NUM;
	txXfer.frame = &txFrame;

	txFrame.dataWord0 = CAN_WORD0_DATA_BYTE_0(0x10) | CAN_WORD0_DATA_BYTE_1(0x20) | CAN_WORD0_DATA_BYTE_2(0x30) |
						CAN_WORD0_DATA_BYTE_3(0x40);
	txFrame.dataWord1 = CAN_WORD1_DATA_BYTE_4(0x50) | CAN_WORD1_DATA_BYTE_5(0x60) | CAN_WORD1_DATA_BYTE_6(0x70) |
						CAN_WORD1_DATA_BYTE_7(0x80);

	for(;;){
		/* Send data through Tx Message Buffer. */
		(void)FLEXCAN_TransferSendNonBlocking(EXAMPLE_CAN, &flexcanHandle, &txXfer);
		if(txComplete){
			LOG_INFO("\r\nSend message from MB%d to MB%d\r\n", TX_MESSAGE_BUFFER_NUM, RX_MESSAGE_BUFFER_NUM);
			LOG_INFO("tx word0 = 0x%x\r\n", txFrame.dataWord0);
			LOG_INFO("tx word1 = 0x%x\r\n", txFrame.dataWord1);
			txComplete = false;
		}else{}
		vTaskDelayUntil( &xLastWakeTime, xPeriod );
	}
}
