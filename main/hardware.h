#ifndef HARDWARE_H
#define HARDWARE_H

// GPIO pins for the 6 relays on Seeed XIAO ESP32-S3
// D0=GPIO1, D1=GPIO2, D2=GPIO3, D3=GPIO4, D4=GPIO5, D5=GPIO6, D8=GPIO7, D9=GPIO8
// D6=GPIO43(TX) en D7=GPIO44(RX) worden vrijgehouden voor UART/debugging
#define RELAY_1_GPIO GPIO_NUM_1
#define RELAY_2_GPIO GPIO_NUM_2
#define RELAY_3_GPIO GPIO_NUM_3
#define RELAY_4_GPIO GPIO_NUM_4
#define RELAY_5_GPIO GPIO_NUM_5
#define RELAY_6_GPIO GPIO_NUM_6
#define RELAY_7_GPIO GPIO_NUM_7
#define RELAY_8_GPIO GPIO_NUM_8

#define NUM_RELAYS 8

// Relay types
#define RELAY_TYPE_NORMAL   0   // Standard: GPIO HIGH = ON, GPIO LOW = OFF
#define RELAY_TYPE_IMPULSE  1   // Bistabel: puls op GPIO om toestand te wisselen

// Pulse duration for impulse relays (milliseconds)
#define RELAY_PULSE_MS  250

// Minimum gap between successive impulse relay pulses (milliseconds)
// Prevents overloading the PSU when multiple relays switch at the same time
#define RELAY_INTER_PULSE_MS  150

// Per-relay type — stel in op RELAY_TYPE_NORMAL of RELAY_TYPE_IMPULSE
#define RELAY_1_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_2_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_3_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_4_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_5_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_6_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_7_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_8_TYPE  RELAY_TYPE_IMPULSE

#endif // HARDWARE_H
