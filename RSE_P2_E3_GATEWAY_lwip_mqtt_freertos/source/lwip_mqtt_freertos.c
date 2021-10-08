/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "lwip/opt.h"

#if LWIP_IPV4 && LWIP_RAW && LWIP_NETCONN && LWIP_DHCP && LWIP_DNS

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_phy.h"

#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dhcp.h"
#include "lwip/netdb.h"
#include "lwip/netifapi.h"
#include "lwip/prot/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "enet_ethernetif.h"
#include "lwip_mqtt_id.h"

#include "ctype.h"
#include "stdio.h"

#include "fsl_phyksz8081.h"
#include "fsl_enet_mdio.h"
#include "fsl_device_registers.h"

/* TODO: insert other include files here. */
#include "fsl_flexcan.h"
#include "FreeRTOS.h"
#include "task.h"
#include "MensajesTxRx.h"
#include "queue.h"

/*******************************************************************************
 * Definitions CAN
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

static u32_t g_KeepAlive = 0;
static u8_t g_bat_freq_counter = 0;
static u8_t g_bat_freq_received = 1;
static char *array_of_messages[6];
static uint32_t ADC_data[6];
static u8_t array_of_messages_index = 0;

uint8_t g_actuator = 0;
uint8_t g_freq = 5;
uint8_t g_delay_samples = 1;   //freq/5
uint8_t g_total_samples = 6;	//30/freq
uint8_t n_sample = 0;

uint8_t g_send_flag = 1;

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

/*******************************************************************************
 * Definitions MQTT
 ******************************************************************************/

/* MAC address configuration. */
#define configMAC_ADDR                     \
    {                                      \
        0x02, 0x12, 0x13, 0x10, 0x15, 0x11 \
    }

/* Address of PHY interface. */
#define EXAMPLE_PHY_ADDRESS BOARD_ENET0_PHY_ADDRESS

/* MDIO operations. */
#define EXAMPLE_MDIO_OPS enet_ops

/* PHY operations. */
#define EXAMPLE_PHY_OPS phyksz8081_ops

/* ENET clock frequency. */
#define EXAMPLE_CLOCK_FREQ CLOCK_GetFreq(kCLOCK_CoreSysClk)

/* GPIO pin configuration. */
#define BOARD_LED_GPIO       BOARD_LED_RED_GPIO
#define BOARD_LED_GPIO_PIN   BOARD_LED_RED_GPIO_PIN
#define BOARD_SW_GPIO        BOARD_SW3_GPIO
#define BOARD_SW_GPIO_PIN    BOARD_SW3_GPIO_PIN
#define BOARD_SW_PORT        BOARD_SW3_PORT
#define BOARD_SW_IRQ         BOARD_SW3_IRQ
#define BOARD_SW_IRQ_HANDLER BOARD_SW3_IRQ_HANDLER


#ifndef EXAMPLE_NETIF_INIT_FN
/*! @brief Network interface initialization function. */
#define EXAMPLE_NETIF_INIT_FN ethernetif0_init
#endif /* EXAMPLE_NETIF_INIT_FN */

/*! @brief MQTT server host name or IP address. */
#define EXAMPLE_MQTT_SERVER_HOST "io.adafruit.com"

/*! @brief MQTT server port number. */
#define EXAMPLE_MQTT_SERVER_PORT 1883

/*! @brief Stack size of the temporary lwIP initialization thread. */
#define INIT_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary lwIP initialization thread. */
#define INIT_THREAD_PRIO DEFAULT_THREAD_PRIO

/*! @brief Stack size of the temporary initialization thread. */
#define APP_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary initialization thread. */
#define APP_THREAD_PRIO DEFAULT_THREAD_PRIO

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static void connect_to_mqtt(void *ctx);

/*******************************************************************************
 * Variables
 ******************************************************************************/

static mdio_handle_t mdioHandle = {.ops = &EXAMPLE_MDIO_OPS};
static phy_handle_t phyHandle   = {.phyAddr = EXAMPLE_PHY_ADDRESS, .mdioHandle = &mdioHandle, .ops = &EXAMPLE_PHY_OPS};

/*! @brief MQTT client data. */
static mqtt_client_t *mqtt_client;

/*! @brief MQTT client ID string. */
static char client_id[40];

/*! @brief MQTT client information. */
static const struct mqtt_connect_client_info_t mqtt_client_info = {
    .client_id   = (const char *)&client_id[0],
    .client_user = "pabloavalosch", //Aquí se coloca el username con el que nos dimos de alta en el broker
    .client_pass = "aio_XTIb202DApdEEJ6cxnXcfhwjQRuG", //No olvidar ponerlo entre comillas
    .keep_alive  = 100,
    .will_topic  = NULL,
    .will_msg    = NULL,
    .will_qos    = 0,
    .will_retain = 0,
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    .tls_config = NULL,
#endif
};

/*! @brief MQTT broker IP address. */
static ip_addr_t mqtt_addr;

/*! @brief Indicates connection to MQTT broker. */
static volatile bool connected = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Called when subscription request finishes.
 */
static void mqtt_topic_subscribed_cb(void *arg, err_t err)
{
    const char *topic = (const char *)arg;

    if (err == ERR_OK)
    {
        PRINTF("Subscribed to the topic \"%s\".\r\n", topic);
    }
    else
    {
        PRINTF("Failed to subscribe to the topic \"%s\": %d.\r\n", topic, err);
    }
}

/*!
 * @brief Called when there is a message on a subscribed topic.
 */
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    LWIP_UNUSED_ARG(arg);
    //PRINTF("Received %u bytes from the topic \"%s\": \"", tot_len, topic);
}

/*!
 * @brief Called when recieved incoming published message fragment.
 */
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    int i;
    uint8_t temp_freq, temp_act;
    //Save before change to compare and know if the message must be sent
    temp_freq = g_freq;
    temp_act = g_actuator;
    LWIP_UNUSED_ARG(arg);
    switch(g_bat_freq)
    {
    case '1':
    	g_freq = 5;
    	break;
    case '2':
    	g_freq = 10;
    	break;
    case '3':
    	g_freq = 15;
    	break;
    case '4':
    	g_actuator = 1;
    	break;
    case '5':
    	g_actuator = 0;
    	break;
    default:
    	break;
    }
    if((temp_freq != g_freq) || (temp_act != g_actuator))
    {
    	g_send_flag = 1;
    	g_delay_samples = g_freq / 5;   //freq/5
    	g_total_samples = 30/g_freq;
    	n_sample = 0;
    	PRINTF("FREQUENCY = %u   ACTUATOR = %u \n", g_freq, g_actuator);
    }
    else
    {
    	g_send_flag = 0;
    }

}

/*!
 * @brief Subscribe to MQTT topics.
 */
static void mqtt_subscribe_topics(mqtt_client_t *client)
{
	static const char *topics[] = {"pabloavalosch/feeds/on-off-actuador", "pabloavalosch/feeds/fcia-nivel-de-bateria"}; //Aquí es para dar de alta los tópicos (feeds)
	int qos[]                   = {0, 0}; //Aquí se definen que van a ser 4 tópicos.
	err_t err;
    int i;

    mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb,
                            LWIP_CONST_CAST(void *, &mqtt_client_info));

    for (i = 0; i < ARRAY_SIZE(topics); i++)
    {
        err = mqtt_subscribe(client, topics[i], qos[i], mqtt_topic_subscribed_cb, LWIP_CONST_CAST(void *, topics[i]));

        if (err == ERR_OK)
        {
            PRINTF("Subscribing to the topic \"%s\" with QoS %d...\r\n", topics[i], qos[i]);
        }
        else
        {
            PRINTF("Failed to subscribe to the topic \"%s\" with QoS %d: %d.\r\n", topics[i], qos[i], err);
        }
    }
}

/*!
 * @brief Called when connection state changes.
 */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    connected = (status == MQTT_CONNECT_ACCEPTED);

    switch (status)
    {
        case MQTT_CONNECT_ACCEPTED:
            PRINTF("MQTT client \"%s\" connected.\r\n", client_info->client_id);
            mqtt_subscribe_topics(client);
            break;

        case MQTT_CONNECT_DISCONNECTED:
            PRINTF("MQTT client \"%s\" not connected.\r\n", client_info->client_id);
            /* Try to reconnect 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_TIMEOUT:
            PRINTF("MQTT client \"%s\" connection timeout.\r\n", client_info->client_id);
            /* Try again 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
        case MQTT_CONNECT_REFUSED_IDENTIFIER:
        case MQTT_CONNECT_REFUSED_SERVER:
        case MQTT_CONNECT_REFUSED_USERNAME_PASS:
        case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
            PRINTF("MQTT client \"%s\" connection refused: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;

        default:
            PRINTF("MQTT client \"%s\" connection status: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;
    }
}

/*!
 * @brief Starts connecting to MQTT broker. To be called on tcpip_thread.
 */
static void connect_to_mqtt(void *ctx)
{
    LWIP_UNUSED_ARG(ctx);

    PRINTF("Connecting to MQTT broker at %s...\r\n", ipaddr_ntoa(&mqtt_addr));

    mqtt_client_connect(mqtt_client, &mqtt_addr, EXAMPLE_MQTT_SERVER_PORT, mqtt_connection_cb,
                        LWIP_CONST_CAST(void *, &mqtt_client_info), &mqtt_client_info);
}

/*!
 * @brief Called when publish request finishes.
 */
static void mqtt_message_published_cb(void *arg, err_t err)
{
    const char *topic = (const char *)arg;

    if (err == ERR_OK)
    {
//        PRINTF("Published to the topic \"%s\".\r\n", topic);
    }
    else
    {
        PRINTF("Failed to publish to the topic \"%s\": %d.\r\n", topic, err);
    }
}

/*!
 * @brief Publishes a message. To be called on tcpip_thread.
 */
static void publish_message(void *ctx)
{
    static const char *topic2   = "pabloavalosch/feeds/estado_nodo_can"; //En cuál tópico quieres publicar?
    static const char *topic1  = "pabloavalosch/feeds/nivel-de-bateria";

    static char *message1 = "2.7V"; //Este valor fue sólo para testear
    static char *message2 = "1";	// "1" prende
    static char *message3 = "0";	// "0" apaga
    if( 0xa == g_KeepAlive ){
        //PRINTF("Going to publish to the topic \"%s\"...\r\n", topic2);
        //Publish the CAN_Node status
        mqtt_publish(mqtt_client, topic2, message2, strlen(message2), 1, 0, mqtt_message_published_cb, (void *)topic2);
        if(n_sample == 0)
        {
        	//FIFO
        	//Publish battery levels
        	for(int i = 0; i< g_total_samples; i++)
        	{
        		if(65000 <= ADC_data[i]) {
        			message1 = "3.3V";
        		} else if(63100 <= ADC_data[i] && 65000 >= ADC_data[i]) {
        			message1 = "3.2V";
        		} else if(61200 <= ADC_data[i] && 63100 >= ADC_data[i]) {
        			message1 = "3.1V";
        		} else if(59300 <= ADC_data[i] && 61200 >= ADC_data[i]) {
        			message1 = "3.0V";
        		} else if(57400 <= ADC_data[i] && 59300 >= ADC_data[i]) {
        			message1 = "2.9V";
        		} else if(55500 <= ADC_data[i] && 57400 >= ADC_data[i]) {
        			message1 = "2.8V";
        		} else if(53600 <= ADC_data[i] && 55500 >= ADC_data[i]) {
        			message1 = "2.7V";
        		} else if(51700 <= ADC_data[i] && 53600 >= ADC_data[i]) {
        			message1 = "2.6V";
        		} else if(49800 <= ADC_data[i] && 51700 >= ADC_data[i]) {
        			message1 = "2.5V";
        		} else if(47900 <= ADC_data[i] && 49800 >= ADC_data[i]) {
        			message1 = "2.4V";
        		} else if(46000 <= ADC_data[i] && 47900 >= ADC_data[i]) {
        			message1 = "2.3V";
        		} else if(44100 <= ADC_data[i] && 46000 >= ADC_data[i]) {
        			message1 = "2.2V";
        		} else if(42200 <= ADC_data[i] && 44100 >= ADC_data[i]) {
        			message1 = "2.1V";
        		} else if(40300 <= ADC_data[i] && 42200 >= ADC_data[i]) {
        			message1 = "2.0V";
        		} else if(38400 <= ADC_data[i] && 40300 >= ADC_data[i]) {
        			message1 = "1.9V";
        		} else if(36500 <= ADC_data[i] && 38400 >= ADC_data[i]) {
        			message1 = "1.8V";
        		} else if(34600 <= ADC_data[i] && 36500 >= ADC_data[i]) {
        			message1 = "1.7V";
        		} else if(32700 <= ADC_data[i] && 34600 >= ADC_data[i]) {
        			message1 = "1.6V";
        		} else if(30800 <= ADC_data[i] && 32700 >= ADC_data[i]) {
        			message1 = "1.5V";
        		} else if(28900 <= ADC_data[i] && 30800 >= ADC_data[i]) {
        			message1 = "1.4V";
        		} else if(27000 <= ADC_data[i] && 28900 >= ADC_data[i]) {
        			message1 = "1.3V";
        		} else if(25100 <= ADC_data[i] && 27000 >= ADC_data[i]) {
        			message1 = "1.2V";
        		} else if(23200 <= ADC_data[i] && 25100 >= ADC_data[i]) {
        			message1 = "1.1V";
        		} else if(21300 <= ADC_data[i] && 23200 >= ADC_data[i]) {
        			message1 = "1.0V";
        		} else if(19400 <= ADC_data[i] && 21300 >= ADC_data[i]) {
        			message1 = "0.9V";
        		} else if(17500 <= ADC_data[i] && 19400 >= ADC_data[i]) {
        			message1 = "0.8V";
        		} else if(15600 <= ADC_data[i] && 17500 >= ADC_data[i]) {
        			message1 = "0.7V";
        		} else if(13700 <= ADC_data[i] && 15600 >= ADC_data[i]) {
        			message1 = "0.6V";
        		} else if(11800 <= ADC_data[i] && 13700 >= ADC_data[i]) {
        			message1 = "0.5V";
        		} else if(9900 <= ADC_data[i] && 11800 >= ADC_data[i]) {
        			message1 = "0.4V";
        		} else if(8000 <= ADC_data[i] && 9900 >= ADC_data[i]) {
        			message1 = "0.3V";
        		} else if(6100 <= ADC_data[i] && 8000 >= ADC_data[i]) {
        			message1 = "0.2V";
        		} else if(4200 <= ADC_data[i] && 6100 >= ADC_data[i]) {
        			message1 = "0.1V";
        		} else {
        			message1 = "0.0V";
        		}
        		mqtt_publish(mqtt_client, topic1, message1, strlen(message1), 1, 0, mqtt_message_published_cb, (void *)topic1);
        	}
        }
    }else{
//        PRINTF("Going to publish to the topic \"%s\"...\r\n", topic2);
        mqtt_publish(mqtt_client, topic2, message3, strlen(message3), 1, 0, mqtt_message_published_cb, (void *)topic2);
    }

    g_KeepAlive = 0x0000000;

    LWIP_UNUSED_ARG(ctx);
}

/*!
 * @brief Application thread.
 */
static void app_thread(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    struct dhcp *dhcp;
    err_t err;
    int i;

    /* Wait for address from DHCP */

    PRINTF("Getting IP address from DHCP...\r\n");

    do
    {
        if (netif_is_up(netif))
        {
            dhcp = netif_dhcp_data(netif);
        }
        else
        {
            dhcp = NULL;
        }

        sys_msleep(20U);

    } while ((dhcp == NULL) || (dhcp->state != DHCP_STATE_BOUND));

    PRINTF("\r\nIPv4 Address     : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
    PRINTF("IPv4 Subnet mask : %s\r\n", ipaddr_ntoa(&netif->netmask));
    PRINTF("IPv4 Gateway     : %s\r\n\r\n", ipaddr_ntoa(&netif->gw));

    /*
     * Check if we have an IP address or host name string configured.
     * Could just call netconn_gethostbyname() on both IP address or host name,
     * but we want to print some info if goint to resolve it.
     */
    if (ipaddr_aton(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr) && IP_IS_V4(&mqtt_addr))
    {
        /* Already an IP address */
        err = ERR_OK;
    }
    else
    {
        /* Resolve MQTT broker's host name to an IP address */
        PRINTF("Resolving \"%s\"...\r\n", EXAMPLE_MQTT_SERVER_HOST);
        err = netconn_gethostbyname(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr);
    }

    if (err == ERR_OK)
    {
        /* Start connecting to MQTT broker from tcpip_thread */
        err = tcpip_callback(connect_to_mqtt, NULL);
        if (err != ERR_OK)
        {
            PRINTF("Failed to invoke broker connection on the tcpip_thread: %d.\r\n", err);
        }
    }
    else
    {
        PRINTF("Failed to obtain IP address: %d.\r\n", err);
    }

    /* Publish some messages */
    for (i = 0; i < 1;)
    {
        if (connected)
        {
            err = tcpip_callback(publish_message, NULL);
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publishing of a message on the tcpip_thread: %d.\r\n", err);
            }
            i++;
        }

        sys_msleep(1000U);
    }

    vTaskDelete(NULL);
}

static void generate_client_id(void)
{
    uint32_t mqtt_id[MQTT_ID_SIZE];
    int res;

    get_mqtt_id(&mqtt_id[0]);

    res = snprintf(client_id, sizeof(client_id), "nxp_%08lx%08lx%08lx%08lx", mqtt_id[3], mqtt_id[2], mqtt_id[1],
                   mqtt_id[0]);
    if ((res < 0) || (res >= sizeof(client_id)))
    {
        PRINTF("snprintf failed: %d\r\n", res);
        while (1)
        {
        }
    }
}

/*!
 * @brief Initializes lwIP stack.
 *
 * @param arg unused
 */
static void stack_init(void *arg)
{
    static struct netif netif;
    ip4_addr_t netif_ipaddr, netif_netmask, netif_gw;
    ethernetif_config_t enet_config = {
        .phyHandle  = &phyHandle,
        .macAddress = configMAC_ADDR,
    };

    LWIP_UNUSED_ARG(arg);
    generate_client_id();

    mdioHandle.resource.csrClock_Hz = EXAMPLE_CLOCK_FREQ;

    IP4_ADDR(&netif_ipaddr, 0U, 0U, 0U, 0U);
    IP4_ADDR(&netif_netmask, 0U, 0U, 0U, 0U);
    IP4_ADDR(&netif_gw, 0U, 0U, 0U, 0U);

    tcpip_init(NULL, NULL);

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        PRINTF("mqtt_client_new() failed.\r\n");
        while (1)
        {
        }
    }

    netifapi_netif_add(&netif, &netif_ipaddr, &netif_netmask, &netif_gw, &enet_config, EXAMPLE_NETIF_INIT_FN,
                       tcpip_input);
    netifapi_netif_set_default(&netif);
    netifapi_netif_set_up(&netif);

    netifapi_dhcp_start(&netif);

    PRINTF("\r\n************************************************\r\n");
    PRINTF(" MQTT client example\r\n");
    PRINTF("************************************************\r\n");

    if (sys_thread_new("app_task", app_thread, &netif, APP_THREAD_STACKSIZE, APP_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("stack_init(): Task creation failed.", 0);
    }

    vTaskDelete(NULL);
}

/*!
 * @brief Main function
 */

int main(void)
{
    SYSMPU_Type *base = SYSMPU;

    /* Initialize board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    /* Disable SYSMPU. */
    base->CESR &= ~SYSMPU_CESR_VLD_MASK;


    /* Initialize lwIP from thread */
    if (sys_thread_new("main", stack_init, NULL, INIT_THREAD_STACKSIZE, INIT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main(): Task creation failed.", 0);
    }

    /* Insert CAN code here (make a function later)*/
    flexcan_config_t flexcanConfig;

    if(xTaskCreate(
      CAN_Rx_Task, /* Pointer to the function that implements the task. */
      "CAN_Rx_Task", /* Text name given to the task. */
	  (configMINIMAL_STACK_SIZE*10), /* The size of the stack that should be created for the task.
                                        This is defined in words, not bytes. */
      NULL,/* A reference to xParameters is used as the task parameter.
              This is cast to a void * to prevent compiler warnings. */
	  (configMAX_PRIORITIES-17), /* The priority to assign to the newly created task. */
      NULL /* The handle to the task being created will be placed in
              xHandle. */
     ) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Rx_Task");
    	while(1);
     }


    if(xTaskCreate(
      CAN_Tx_Task, /* Pointer to the function that implements the task. */
      "CAN_Tx_Task", /* Text name given to the task. */
	  (configMINIMAL_STACK_SIZE*10), /* The size of the stack that should be created for the task.
                                        This is defined in words, not bytes. */
      NULL,/* A reference to xParameters is used as the task parameter.
              This is cast to a void * to prevent compiler warnings. */
	  (configMAX_PRIORITIES-16), /* The priority to assign to the newly created task. */
      NULL /* The handle to the task being created will be placed in
              xHandle. */
     ) != pdPASS )
     {
    	LOG_INFO("Failing to create CAN_Tx_Task");
    	while(1);
     }

    LOG_INFO("\r\n==FlexCAN loopback example -- Start.==\r\n\r\n");

    /* Init FlexCAN module. */
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

    /* Will not get here unless a task calls vTaskEndScheduler ()*/
    return 0;
}
#endif

void CAN_Rx_Task(void* Args){
	TickType_t xLastWakeTime;
	const TickType_t xPeriod = pdMS_TO_TICKS( 5000 );	// Keep Alive every 5 seconds
	xLastWakeTime = xTaskGetTickCount();
	static uint8_t delay_counter = 0;

    err_t err;
    int i;

	/* Setup Rx Message Buffer. */
    mbConfig.format = kFLEXCAN_FrameFormatStandard;
    mbConfig.type   = kFLEXCAN_FrameTypeData;
    mbConfig.id     = FLEXCAN_ID_STD(0x123);	// ID cruzado Rx - Tx

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

        	g_KeepAlive = rxFrame.dataWord0;
    		//Choose to save the sample or not
    		if(delay_counter == 0)
    		{
    			//ADC reading
    			ADC_data[n_sample] = rxFrame.dataWord1;
    			n_sample++;
    		}
    		delay_counter++;
    		if(delay_counter == g_delay_samples)
    		{
    			delay_counter = 0;
    		}
    		if(n_sample == g_total_samples)
    		{
    			//Restart samples
    			n_sample = 0;
    		}
        	rxComplete = false;
    	}else {
    		g_KeepAlive = 0x0;
    	}
		err = tcpip_callback(publish_message, NULL);
		if (err != ERR_OK)
		{
			PRINTF("Failed to invoke publishing of a message on the tcpip_thread: %d.\r\n", err);
		}
    	/* Publish some messages */
		vTaskDelayUntil( &xLastWakeTime, xPeriod );
    }
}



void CAN_Tx_Task(void* Args){
	TickType_t xLastWakeTime;
	const TickType_t xPeriod = pdMS_TO_TICKS( 100 );	// Keep Alive every 5 seconds
	xLastWakeTime = xTaskGetTickCount();

	/* Setup Tx Message Buffer. */
	FLEXCAN_SetTxMbConfig(EXAMPLE_CAN, TX_MESSAGE_BUFFER_NUM, true);

	/* Prepare Tx Frame for sending. */
	txFrame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
	txFrame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
	txFrame.id     = FLEXCAN_ID_STD(0x124);		// ID cruzado Tx - Rx
	txFrame.length = (uint8_t)DLC;

	txXfer.mbIdx = (uint8_t)TX_MESSAGE_BUFFER_NUM;
	txXfer.frame = &txFrame;

	for(;;){
		/* Send data through Tx Message Buffer. */
		//Send by event, if the frequency or the act value has changed
		if(g_send_flag)
		{
			txFrame.dataWord0 = g_freq;
			txFrame.dataWord1 = g_actuator;
			(void)FLEXCAN_TransferSendNonBlocking(EXAMPLE_CAN, &flexcanHandle, &txXfer);
			if(txComplete){
				LOG_INFO("\r\nSend message from MB%d to MB%d\r\n", TX_MESSAGE_BUFFER_NUM, RX_MESSAGE_BUFFER_NUM);
				LOG_INFO("tx word0 = 0x%x\r\n", txFrame.dataWord0);
				LOG_INFO("tx word1 = 0x%x\r\n", txFrame.dataWord1);
				g_send_flag = 0;
				txComplete = false;
			}else{}
		}
		vTaskDelayUntil( &xLastWakeTime, xPeriod );
	}
}
