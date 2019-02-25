#include <stdio.h>
#include <stdlib.h>

// see: http://forum.arduino.cc/index.php?topic=235453.0
//      http://gammon.com.au/interrupts

// Define the pin
#define DATA 0  // 2  // pin 14 of 74HC595 (SDI)     (DIO)  9->14
#define RCLK 1  // 3  // pin 12 of 74HC595 (LCHCLK)  (RCLK)
#define SCLK 2  // 5  // pin 11 of 74HC595 (SFTCLK)  (SCLK)

#define HC_DATA_H digitalWrite(DATA, HIGH)
#define HC_DATA_L digitalWrite(DATA, LOW)
#define HC_RCLK_H digitalWrite(RCLK, HIGH)
#define HC_RCLK_L digitalWrite(RCLK, LOW)
#define HC_SCLK_H digitalWrite(SCLK, HIGH)
#define HC_SCLK_L digitalWrite(SCLK, LOW)

typedef union {
	unsigned short data;
	struct {
		unsigned short position : 4;  // bit [0-3]  - display position
		unsigned short reserved : 4;
		unsigned short code     : 7;  // bit [9-14] - LED out code
		unsigned short period   : 1;  // bit [15]   - LED decimal period
	} s;
} led_module_data_t;


// 7-segment digital tube BCD codes
#define MINUS_CODE 	0b01000000
static const unsigned char led_bcd_map[16] = {
//
//	0b11000000, // (0) 0xC0  // original
//	0b11111001, // (1) 0xF9
//	0b10100100, // (2) 0xA4
//	0b10110000, // (3) 0xB0
//	0b10011001, // (4) 0x99
//	0b10010010, // (5) 0x92
//	0b10000010, // (6) 0x82
//	0b11111000, // (7) 0xF8
//	0b10000000, // (8) 0x80
//	0b10010000, // (9) 0x90
//	0b10001000, // (A) 0x88
//	0b10000011, // (B) 0x83
//	0b11000110, // (C) 0xC6
//	0b10100001, // (D) 0xA1
//	0b10000110, // (E) 0x86
//	0b10001110, // (F) 0x8E
//
	0b00111111, // (0) 0b3F  // inverted
	0b00000110, // (1) 0b06
	0b01011011, // (2) 0b5B
	0b01001111, // (3) 0b4F
	0b01100110, // (4) 0b66
	0b01101101, // (5) 0b6D
	0b01111101, // (6) 0b7D
	0b00000111, // (7) 0b07
	0b01111111, // (8) 0b7F
	0b01101111, // (9) 0b6F
	0b01110111, // (A) 0b77
	0b01111100, // (B) 0b7C
	0b00111001, // (C) 0b39
	0b01011110, // (D) 0b5E
	0b01111001, // (E) 0b79
	0b01110001, // (F) 0b71
//
//	0b11111100, // (0) 0xFC  // inverted, bit-swapped
//	0b01100000, // (1) 0x60
//	0b11011010, // (2) 0xDA
//	0b11110010, // (3) 0xF2
//	0b01100110, // (4) 0x66
//	0b10110110, // (5) 0xB6
//	0b10111110, // (6) 0xBE
//	0b11100000, // (7) 0xE0
//	0b11111110, // (8) 0xFE
//	0b11110110, // (9) 0xF6
//	0b11101110, // (A) 0xEE
//	0b00111110, // (B) 0x3E
//	0b10011100, // (C) 0x9C
//	0b01111010, // (D) 0x7A
//	0b10011110, // (E) 0x9E
//	0b10001110, // (F) 0x8E
};

static volatile unsigned short display_buf[4];  // shared data buffer for ISR


// Arduino setup
void setup()
{
	pinMode(DATA, OUTPUT);
	pinMode(RCLK, OUTPUT);
	pinMode(SCLK, OUTPUT);
	HC_RCLK_L;
	HC_SCLK_L;

	// set up Timer 1
	TCCR1A = 0;                                   // normal operation
//	TCCR1B = bit(WGM12) | bit(CS10);              // CTC, no pre-scaling        1/(64/16000000)      = 250 kHz
	TCCR1B = bit(WGM12) | bit(CS10) | bit (CS12); // CTC, scale to clock/1024   1/(64*1024/16000000) = 244 Hz
	OCR1A =  63;            // compare A register value (1000 * clock speed)
	TIMSK1 = bit (OCIE1A);  // interrupt on Compare A Match
}

// Arduino loop
void loop()
{
	static unsigned int value = 0;
	static unsigned long prev_millis = millis();
	unsigned long curr_millis = millis();

	if (curr_millis - prev_millis > 50)
	{
	    value++;
	    prev_millis = curr_millis;
	}

//	// Hexadecimal test output
//	LedDisplay(0, (value      ) & 0x0F, 0);
//	LedDisplay(1, (value >> 4 ) & 0x0F, 0);
//	LedDisplay(2, (value >> 8 ) & 0x0F, 0);
//	LedDisplay(3, (value >> 12) & 0x0F, 1);

	// Decimal test output
	LedDisplay(0, value        % 10, 0);
	LedDisplay(1, value / 10   % 10, 0);
	LedDisplay(2, value / 100  % 10, 0);
	LedDisplay(3, value / 1000 % 10, 1);

//	LedDisplay(3, '-'              , 1);
}

void LedDisplay(unsigned char position, unsigned char symbol, int period)
{
	unsigned short ledcode = 0;

	if (position >= 4)
	{
		position = 0;
		symbol = '-';
	}

	// Hexadecimal symbols
	if (symbol < 16)
		ledcode = led_bcd_map[symbol];

	// Minus symbol
	else if (symbol == '-')
		ledcode = MINUS_CODE;

	ledcode <<= 8;             // bit [9-14] - LED out code

	// Decimal period
	if (period != 0)
		ledcode |= 0x8000;     // bit [15]   - LED decimal period

	ledcode |= 1 << position;  // bit [0-3]  - display position

	display_buf[position] = ledcode ^ 0xFF00;
}

// Scanning output to LED tube (74HC595)
// Interrupt Service Routine (ISR)
ISR(TIMER1_COMPA_vect)
{
	static int position = 0;
	unsigned short data = display_buf[position];
	if (++position >= 4)
		position = 0;

	for (unsigned short mask = 0x8000; mask != 0; mask >>= 1)
	{
		if (data & mask)
			HC_DATA_H;
		else
			HC_DATA_L;

		HC_SCLK_H;
		HC_SCLK_L;  // send data to shift register
	}

	HC_RCLK_L;
	HC_RCLK_H;  // store data in latch register
}



