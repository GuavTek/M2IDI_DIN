/*
 * RingBuffer.h
 *
 * Created: 23-Sep-20 16:02:22
 *  Author: GuavTek
 */ 


#ifndef RINGBUFFER_H_
#define RINGBUFFER_H_

template <uint16_t BUFFER_SIZE>
class RingBuffer
{
public:
	uint8_t Read();
	uint8_t Peek();
	void Write(uint8_t in);
	uint8_t Count();
	void Flush();
	const uint8_t length = BUFFER_SIZE;
	//RingBuffer();
	
private:
	uint8_t buffer[BUFFER_SIZE];
	uint8_t head = 0;
	uint8_t tail = 0;
};

/*
template <uint16_t BUFFER_SIZE>
RingBuffer<BUFFER_SIZE>::RingBuffer(){
	tail = 0;
	head = 0;
	length = BUFFER_SIZE;
}
*/

//Read the next byte in buffer
template <uint16_t BUFFER_SIZE>
uint8_t RingBuffer<BUFFER_SIZE>::Read(){
	if (Count() > 0)
	{
		tail++;
		if (tail >= length)
		{
			tail = 0;
		}
		return buffer[tail];
	}
	return 0;
}

//Read next byte without incrementing pointers
template <uint16_t BUFFER_SIZE>
uint8_t RingBuffer<BUFFER_SIZE>::Peek(){
	uint8_t tempTail = tail + 1;
	
	if (tempTail >= length)
	{
		tempTail = 0;
	}
	
	return buffer[tempTail];
}

//Write a byte to the buffer
template <uint16_t BUFFER_SIZE>
void RingBuffer<BUFFER_SIZE>::Write(uint8_t in){
	if (Count() < length - 2)
	{
		head++;
		
		if (head >= length)
		{
			head = 0;
		}
		
		buffer[head] = in;
	}
}

//Returns how many elements are in the buffer
template <uint16_t BUFFER_SIZE>
uint8_t RingBuffer<BUFFER_SIZE>::Count(){
	//Compensate for overflows
	if (head >= tail)
	{
		return (head - tail);
	} else {
		return (head - tail + length);
	}
}

//Resets buffer
template <uint16_t BUFFER_SIZE>
void RingBuffer<BUFFER_SIZE>::Flush(){
	tail = 0;
	head = 0;
}


#endif /* RINGBUFFER_H_ */