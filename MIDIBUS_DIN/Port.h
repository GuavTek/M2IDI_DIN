/*
 * Port.h
 *
 * Created: 17/07/2021 23:06:21
 *  Author: mikda
 */ 


#ifndef PORT_H_
#define PORT_H_

inline void port_pin_set_output_level(
uint8_t gpio_pin,
bool level)
{
	uint8_t pin_group;
	if (gpio_pin >= 32) {
		pin_group = 1;
		} else {
		pin_group = 0;
	}
	uint32_t pin_mask  = (1UL << (gpio_pin % 32));

	/* Set the pin to high or low atomically based on the requested level */
	if (level) {
		PORT->Group[pin_group].OUTSET.reg = pin_mask;
		} else {
		PORT->Group[pin_group].OUTCLR.reg = pin_mask;
	}
}




#endif /* PORT_H_ */