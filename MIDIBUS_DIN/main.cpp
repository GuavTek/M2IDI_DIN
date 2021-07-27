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

int main(void)
{
	PORT->Group[0].DIRSET.reg = 1 << 21;
	PORT->Group[0].DIRSET.reg = 1 << 17;
	
	port_pin_set_output_level(17, 0);
	port_pin_set_output_level(21, 1);
    /* Replace with your application code */
    while (1) 
    {
    }
}

void WDT_Init(){
	volatile uint32_t temp;
	WDT->CTRL.bit.ALWAYSON = 0;
	temp = 69;
	WDT->CTRL.bit.ENABLE = 0;
}