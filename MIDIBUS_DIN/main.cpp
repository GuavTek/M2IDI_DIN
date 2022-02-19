/*
 * MIDIBUS_DIN.cpp
 *
 * Created: 02-Jun-21 23:21:46
 * Author : GuavTek
 */ 

#include <asf.h>
#include "samd21g15b.h"
#include "MIDI_Driver.h"
#include "MIDI_Config.h"
#include "SPI.h"
#include "MCP2517.h"
#include "RingBuffer.h"

MCP2517_C CAN(SERCOM0);

MIDI_C canMIDI(2);
MIDI_C dinMIDI(1);

uint8_t group = 1;

// Should be randomly generated
uint32_t midiID = 42;

void Receive_CAN(CAN_Rx_msg_t* msg);

void MIDI_CAN_Handler(MIDI1_msg_t* msg);
void MIDI_DIN_Handler(MIDI1_msg_t* msg);

#define UART_BAUDRATE 31250
#define UART_BAUD_VAL 8000000/(16 * UART_BAUDRATE)
void UART_Init();
RingBuffer<250> tx_buff;
RingBuffer<32> rx_buff;

void RTC_Init();

int main(void)
{
	
	system_init();
	
	// LEDs on A17 and A21
	PORT->Group[0].DIRSET.reg = 1 << 21;
	PORT->Group[0].DIRSET.reg = 1 << 17;
	
	port_pin_set_output_level(17, 0);
	port_pin_set_output_level(21, 0);
	
	
	// Switch on A19
	PORT->Group[0].PINCFG[19].bit.INEN = 1;	
	
	UART_Init();
	CAN.Init(CAN_CONF, SPI_CONF);
	RTC_Init();
	
	canMIDI.Set_handler(MIDI_CAN_Handler);
	dinMIDI.Set_handler(MIDI_DIN_Handler);
	
	NVIC_EnableIRQ(SERCOM0_IRQn);
	system_interrupt_enable_global();
	
    while (1) {
		CAN.State_Machine();
		
		while (rx_buff.Count() > 0){
			char temp = rx_buff.Read();
			dinMIDI.Decode(&temp, 1);
		}
		
		static uint32_t periodic_timer = 0;
		if (periodic_timer < RTC->MODE0.COUNT.reg)	{
			periodic_timer = RTC->MODE0.COUNT.reg + 30;
			// Reset activity LEDs
			port_pin_set_output_level(21, 0);
			port_pin_set_output_level(17, 0);
		}
    }
}

void Receive_CAN(CAN_Rx_msg_t* msg){
	uint8_t length = CAN.Get_Data_Length(msg->dataLengthCode);
	canMIDI.Decode(msg->payload, length);
}

void MIDI_CAN_Handler(MIDI1_msg_t* msg){
	// DIN output Activity LED
	port_pin_set_output_level(21, 1);
	
	char buff[4];
	// Convert to byte stream
	uint8_t length = canMIDI.Encode(buff, msg, 1);
	
	// Add to uart tx buffer, should maybe have overflow protection?
	for (uint8_t i = 0; i < length; i++){
		tx_buff.Write(buff[i]);
	}
	
	// Enable UART DRE interrupt
	SERCOM2->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
}

void MIDI_DIN_Handler(MIDI1_msg_t* msg){
	// DIN input activity LED
	port_pin_set_output_level(17, 1);
	char buff[4];
	// Convert to byte stream
	msg->group = group;
	uint8_t length = dinMIDI.Encode(buff, msg, 2);
	
	// Send CAN message
	CAN_Tx_msg_t outMsg;
	outMsg.dataLengthCode = CAN.Get_DLC(length);
	outMsg.id = midiID;
	outMsg.payload = buff;
	CAN.Transmit_Message(&outMsg, 2);
	
	// Thru switch
	if ( !(PORT->Group[0].IN.reg & (1 << 19)) ){
		MIDI_CAN_Handler(msg);
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
	SERCOM2->USART.CTRLA.bit.SAMPR = 1;
	SERCOM2->USART.CTRLA.bit.CMODE = 0; // Async
	SERCOM2->USART.CTRLA.bit.RXPO = 0x2; // pad 2
	SERCOM2->USART.CTRLA.bit.TXPO = 0x0; // pad 0
	SERCOM2->USART.CTRLA.bit.DORD = 1;
	
	
	while(SERCOM2->USART.SYNCBUSY.bit.CTRLB);
	SERCOM2->USART.BAUD.FRAC.BAUD = UART_BAUD_VAL;
	SERCOM2->USART.BAUD.FRAC.FP = 0;
	
	while(SERCOM2->USART.SYNCBUSY.bit.CTRLB);
	SERCOM2->USART.CTRLB.reg |= SERCOM_USART_CTRLB_RXEN | SERCOM_USART_CTRLB_TXEN;
	
	while(SERCOM2->USART.SYNCBUSY.bit.ENABLE);
	SERCOM2->USART.INTENSET.bit.RXC = 1;
	
	while(SERCOM2->USART.SYNCBUSY.bit.ENABLE);
	SERCOM2->USART.CTRLA.bit.ENABLE = 1;
	NVIC_EnableIRQ(SERCOM2_IRQn);
}

void RTC_Init(){
	// Enable clock
	PM->APBAMASK.bit.RTC_ = 1;
	
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_ID_RTC;
	
	RTC->MODE0.READREQ.bit.RCONT = 1;
	
	RTC->MODE0.COUNT.reg = 0;
	
	RTC->MODE0.CTRL.bit.MODE = RTC_MODE0_CTRL_MODE_COUNT32_Val;
	RTC->MODE0.CTRL.bit.PRESCALER = RTC_MODE0_CTRL_PRESCALER_DIV32_Val;
	
	RTC->MODE0.CTRL.bit.ENABLE = 1;
}

void SERCOM0_Handler(){
	CAN.Handler();
}

 void SERCOM2_Handler(){
	if (SERCOM2->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_DRE){
		if (tx_buff.Count() > 0){
			SERCOM2->USART.DATA.reg = tx_buff.Read();
		} else {
			SERCOM2->USART.INTENCLR.reg = SERCOM_USART_INTENCLR_DRE; 
		}
	}
	if (SERCOM2->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_RXC){
		SERCOM2->USART.INTFLAG.reg = SERCOM_USART_INTFLAG_RXC;
		rx_buff.Write(SERCOM2->USART.DATA.reg);
	}
}
