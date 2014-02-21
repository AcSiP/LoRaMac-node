/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    ©2013 Semtech

Description: Implements the STM32 internal ADC block

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#ifndef __ADC_H__
#define __ADC_H__

/*!
 * ADC object type definition
 */
typedef struct
{
    ADC_TypeDef *Adc;
    Gpio_t AdcInput;
}Adc_t;

/*!
 * \brief Initializes the ADC input
 *
 * \param [IN] obj  ADC object
 * \param [IN] scl  ADC input pin name to be used
 */
void AdcInit( Adc_t *obj, PinNames adcInput );

/*!
 * \brief DeInitializes the ADC 
 *
 * \param [IN] obj  ADC object
 */
void AdcDeInit( Adc_t *obj );

/*!
 * \brief Read the analogue voltage value
 *
 * \param [IN] obj  ADC object
 */
uint16_t AdcReadChannel( Adc_t *obj );

#endif  // __ADC_H__
