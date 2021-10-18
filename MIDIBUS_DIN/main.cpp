/*
 * MIDIBUS_DIN.cpp
 *
 * Created: 02-Jun-21 23:21:46
 * Author : GuavTek
 */ 


#include "sam.h"
#include "Port.h"
#include "MIDI_Driver.h"

void WDT_Init();
#define UART_BAUDRATE 31250
#define UART_BAUD_VAL 65536*(1-16*(UART_BAUDRATE/8000000))
void UART_Init();
RingBuffer<250> tx_buff;
RingBuffer<32> rx_buff;

int main(void)
{
	UART_Init();
	PORT->Group[0].DIRSET.reg = 1 << 21;
	PORT->Group[0].DIRSET.reg = 1 << 17;
	
	port_pin_set_output_level(17, 0);
	port_pin_set_output_level(21, 1);
	
	system_interrupt_enable_global();
	
    while (1) {
    }
}

void UART_Init(){
	//Setting the Software Reset bit to 1
	SERCOM2->USART.CTRLA.bit.SWRST = 1;
	while(SERCOM2->USART.CTRLA.bit.SWRST || SERCOM2->USART.SYNCBUSY.bit.SWRST);
	
	// Enable SERCOM clock
	PM->APBCMASK.reg |= PM_APBCMASK_SERCOM2;
	
	// Select generic clock
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_ID_SERCOM2_CORE;
	
	// Set pinmux
	pin_set_peripheral_function(PINMUX_PA12C_SERCOM2_PAD0);
	pin_set_peripheral_function(PINMUX_PA14C_SERCOM2_PAD2);
	
	SERCOM2->USART.CTRLA.bit.MODE = SERCOM_USART_CTRLA_MODE_USART_INT_CLK_Val;
	SERCOM2->USART.CTRLA.bit.SAMPR = 0;
	SERCOM2->USART.CTRLA.bit.CMODE = 0; // Async
	SERCOM2->USART.CTRLA.bit.RXPO = 0x2; // pad 2
	SERCOM2->USART.CTRLA.bit.TXPO = 0x2; // pad 0
	SERCOM2->USART.INTENSET.bit.RXC = 1;
	
	SERCOM2->USART.BAUD.reg = UART_BAUD_VAL;
	
	SERCOM2->USART.CTRLB.reg |= SERCOM_USART_CTRLB_RXEN | SERCOM_USART_CTRLB_TXEN;
	
	SERCOM2->USART.CTRLA.bit.ENABLE = 1;
	NVIC_EnableIRQ(SERCOM2_IRQn);
}

 void SERCOM2_Handler(){
	if (SERCOM2->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_DRE){
		if (tx_buff.Count() > 0){
			SERCOM2->USART.DATA = tx_buff.Read();
		} else {
			SERCOM2->USART.INTENCLR.reg = SERCOM_USART_INTENCLR_DRE; 
		}
	}
	if (SERCOM2->USART.INTFLAG & SERCOM_USART_INTFLAG_RXC){
		SERCOM2->USART.INTFLAG = SERCOM_USART_INTFLAG_RXC;
		rx_buff.Write(SERCOM2->USART.DATA);
	}
}
