/*
 * MensajesTxRx.h
 *
 *  Created on: 3 oct 2021
 *      Author: RSEO2021 EQ3
 */

#ifndef MENSAJESTXRX_H_
#define MENSAJESTXRX_H_

#define KeepAlive_ID	(0x100)
#define NivelBateria_ID	(0x25)
#define FrecReporBat_ID (0x55)
#define ComandoActua_ID (0x80)

#define KeepAlive_Value (0x0A)	// 8 bits
uint16_t g_NivelBateria = 0;	// 16 bits
uint8_t g_FrecReporBat = 5;		// 8 bits
bool g_ComandoActua = false;	// 1 bit

#define FrecReporte1  5U
#define FrecReporte2 10U
#define FrecReporte3 15U

#define ON  1
#define OFF 0

#endif /* MENSAJESTXRX_H_ */
