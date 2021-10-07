/*
 * ADC16.h
 *
 *  Created on: 6 oct. 2021
 *      Author: ferna
 */

#ifndef ADC16_H_
#define ADC16_H_

#include "fsl_adc16.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEMO_ADC16_BASE          ADC0
#define DEMO_ADC16_CHANNEL_GROUP 0U
#define DEMO_ADC16_USER_CHANNEL  12U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

void ADC16_init();
uint32_t ADC16_read();

#endif /* ADC16_H_ */
