/*
 * dsp_uart2.h
 *
 *  Created on: 2018-12-8
 *      Author: zhtro
 */

#ifndef DSP_UART2_H_
#define DSP_UART2_H_

#include "stdint.h"
#include "uart.h"

typedef void (*DSPUART2_Handler)(uint32_t event, unsigned char data);

extern void UART2StdioInit();
extern unsigned int UART2Puts(char *pTxBuffer, int numBytesToWrite);
extern unsigned int UART2Send(const char *pcBuf, unsigned int len);
unsigned char UART2Getc(void);
extern void UART2RegisterHandler(DSPUART2_Handler hdl);

#endif /* DSP_UART2_H_ */
