/*
 * main.cpp
 *
 * Created: 02-Jun-21 23:21:46
 * Author : GuavTek
 */ 

#include <asf.h>
#include <sam.h>
#include "MIDI_Driver.h"
#include "SPI_SAMD.h"
#include "MCP2517.h"
#include "MIDI_Config.h"
#include "RingBuffer.h"

SPI_SAMD_C SPI(SERCOM0, 1);
MCP2517_C CAN(&SPI, 0);

MIDI_C canMIDI(2);
MIDI_C dinMIDI(1);

uint8_t midiGroup;
uint32_t midiID;

void Receive_CAN(CAN_Rx_msg_t* msg);
void Receive_CAN_Payload(char* data, uint8_t length);
void Check_CAN_Int();

void MIDI_CAN_Handler(MIDI_UMP_t* msg);
void MIDI_DIN_Handler(MIDI_UMP_t* msg);

uint8_t Get_Group();

#define UART_BAUDRATE 31250
#define UART_BAUD_VAL 8000000/(16 * UART_BAUDRATE)
void UART_Init();
RingBuffer<128> tx_buff;
RingBuffer<32> rx_buff;
bool hasLost = false;
MIDI_UMP_t lostMsg;

void RTC_Init();

int main(void)
{
	system_init();
	SPI.Init(SPI_CONF);
	// LEDs on A17 and A21
	PORT->Group[0].DIRSET.reg = 1 << 21;
	PORT->Group[0].DIRSET.reg = 1 << 17;
	
	port_pin_set_output_level(17, 0);
	port_pin_set_output_level(21, 0);
	
	// Configure the CAN_INT pin
	struct port_config intCon = {
		.direction = PORT_PIN_DIR_INPUT,
		.input_pull = PORT_PIN_PULL_NONE,
		.powersave = false
	};
	port_pin_set_config(PIN_PA01, &intCon);
	
	// DIP switch on PB08-PB11
	PORT->Group[1].PINCFG[8].reg = PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;
	PORT->Group[1].PINCFG[9].reg = PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;
	PORT->Group[1].PINCFG[10].reg = PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;
	PORT->Group[1].PINCFG[11].reg = PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;
	PORT->Group[1].OUTSET.reg = 0x0F00;
	
	// Switch on A19
	PORT->Group[0].PINCFG[19].bit.INEN = 1;	
	
	UART_Init();
	RTC_Init();
	
	NVIC_EnableIRQ(SERCOM0_IRQn);
	system_interrupt_enable_global();
	
	canMIDI.Set_handler(MIDI_CAN_Handler);
	canMIDI.Set_channel_mask(0xffff);
	dinMIDI.Set_handler(MIDI_DIN_Handler);
	dinMIDI.Set_channel_mask(0xffff);
	dinMIDI.Set_group_mask(0xffff);
	
	// Randomize ID
	midiID = rand();
	midiGroup = 0;
	
	CAN.Set_Rx_Header_Callback(Receive_CAN);
	CAN.Set_Rx_Data_Callback(Receive_CAN_Payload);
	CAN.Init(CAN_CONF);
	// Set randomized ID
	CAN_Filter_t tempFilt = CAN_FLT2;
	tempFilt.ID = midiID & 0x7F;
	CAN.Reconfigure_Filter(&tempFilt, 2);
	
    while (1) {
		while (rx_buff.Count() > 0){
			char temp = rx_buff.Read();
			dinMIDI.Decode(&temp, 1);
		}
		
		if (midiGroup != Get_Group()){
			midiGroup = Get_Group();
			CAN_Filter_t tempFilt = CAN_FLT0;
			tempFilt.ID = (midiGroup & 0xF) << 7;
			CAN.Reconfigure_Filter(&tempFilt, 0);
		}
		
		if (CAN.Ready()){
			if (hasLost){
				// Send realtime message which stalled due to sysex
				MIDI_DIN_Handler(&lostMsg);
			} else if (tx_buff.Count() <= 64){
				// Try to fetch data if buffer has room for largest possible frame
				Check_CAN_Int();
			}
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

void Check_CAN_Int(){
	if (!port_pin_get_input_level(PIN_PA01)){
		CAN.Check_Rx();
	}
}

void Receive_CAN(CAN_Rx_msg_t* msg){
	uint8_t length = CAN.Get_Data_Length(msg->dataLengthCode);
	if ((msg->id & 0x7F) == (midiID & 0x7F)){
		// Received a message using the same ID. Reconfigure.
		midiID = rand();
	}
}

void Receive_CAN_Payload(char* data, uint8_t length){
	canMIDI.Decode(data, length);
}

void MIDI_CAN_Handler(MIDI_UMP_t* msg){
	char buff[8];
	uint8_t length = 0;
	// Convert to byte stream
	length = canMIDI.Encode(buff, msg, 1);
	
	if (length == 0){
		return;
	}
	
	// DIN output Activity LED
	port_pin_set_output_level(17, 1);
	
	// Add to uart tx buffer, should maybe have overflow protection?
	for (uint8_t i = 0; i < length; i++){
		tx_buff.Write(buff[i]);
	}
	
	// Enable UART DRE interrupt
	SERCOM2->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
}

// TODO: Handle cases where CAN controller memory is full?
void MIDI_DIN_Handler(MIDI_UMP_t* msg){
	static int8_t numBytes = 0;
	// DIN input activity LED
	port_pin_set_output_level(21, 1);
	char buff[8];
	// Convert to byte stream
	msg->voice1.group = Get_Group();
	uint8_t length = dinMIDI.Encode(buff, msg, 2);
	
	if (numBytes == 0){
		// Send first UMP
		CAN_Tx_msg_t outMsg;
		outMsg.dataLengthCode = CAN.Get_DLC(length);
		outMsg.id = (midiID & 0x7F)|(int(msg->voice1.group) << 7);
		outMsg.payload = buff;
		outMsg.bitrateSwitch = true;
		CAN.Write_Message(&outMsg, 2);
	} else {
		CAN.Append_Payload(buff, length);
	}
	numBytes += length;
	
	if (msg->type == MIDI_MT_E::Data64){
		// Collect Sysex sub-packets
		if (msg->data64.status == MIDI2_DATA64_E::Single){
			CAN.Send_Message();
			numBytes = 0;
		} else if (msg->data64.status == MIDI2_DATA64_E::End){
			CAN.Send_Message();
			numBytes = 0;
		} else if (numBytes > 56){
			CAN.Send_Message();
			numBytes = 0;
		}
	} else {
		// Send CAN message
		CAN.Send_Message();
		numBytes = 0;
	}
	
	// Thru switch
	if ( !(PORT->Group[0].IN.reg & (1 << 19)) ){
		MIDI_CAN_Handler(msg);
	}
	hasLost = false;
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
	
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2 | GCLK_CLKCTRL_ID_RTC;
	
	RTC->MODE0.READREQ.bit.RCONT = 1;
	
	RTC->MODE0.COUNT.reg = 0;
	
	RTC->MODE0.CTRL.bit.MODE = RTC_MODE0_CTRL_MODE_COUNT32_Val;
	RTC->MODE0.CTRL.bit.PRESCALER = RTC_MODE0_CTRL_PRESCALER_DIV32_Val;
	
	RTC->MODE0.CTRL.bit.ENABLE = 1;
}

uint8_t Get_Group(){
	uint8_t group;
	uint8_t temp;
	// Get input
	group = (PORT->Group[1].IN.reg & 0x0F00) >> 8;
	// Swap bits because switch is connected in reverse
	temp = ((group & 0b1100) >> 2)|((group & 0b0011) << 2);
	group = ((temp & 0b1010) >> 1)|((temp & 0b0101) << 1);
	group = ~group;
	return group & 0x0f;
}

void SERCOM0_Handler(){
	SPI.Handler();
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
		char temp = SERCOM2->USART.DATA.reg;
		rx_buff.Write(temp);
	}
}
