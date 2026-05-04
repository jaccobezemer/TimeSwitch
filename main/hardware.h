#ifndef HARDWARE_H
#define HARDWARE_H

// GPIO pins for the 8 relays on LILYGO T7-S3
#define RELAY_1_GPIO GPIO_NUM_14
#define RELAY_2_GPIO GPIO_NUM_5
#define RELAY_3_GPIO GPIO_NUM_6
#define RELAY_4_GPIO GPIO_NUM_7
#define RELAY_5_GPIO GPIO_NUM_8
#define RELAY_6_GPIO GPIO_NUM_9
#define RELAY_7_GPIO GPIO_NUM_10
#define RELAY_8_GPIO GPIO_NUM_38

// #define RELAY_1_GPIO GPIO_NUM_4
// #define RELAY_2_GPIO GPIO_NUM_5
// #define RELAY_3_GPIO GPIO_NUM_6
// #define RELAY_4_GPIO GPIO_NUM_7
// #define RELAY_5_GPIO GPIO_NUM_8
// #define RELAY_6_GPIO GPIO_NUM_9
// #define RELAY_7_GPIO GPIO_NUM_10
// #define RELAY_8_GPIO GPIO_NUM_38

#define NUM_RELAYS 8

// Relay types
#define RELAY_TYPE_NORMAL   0   // Standard: GPIO HIGH = ON, GPIO LOW = OFF
#define RELAY_TYPE_IMPULSE  1   // Bistabel: puls op GPIO om toestand te wisselen

// Pulse duration for impulse relays (milliseconds)
#define RELAY_PULSE_MS  500

// Minimum gap between successive impulse relay pulses (milliseconds)
// Prevents overloading the PSU when multiple relays switch at the same time
#define RELAY_INTER_PULSE_MS  150

// Per-relay type — stel in op RELAY_TYPE_NORMAL of RELAY_TYPE_IMPULSE
#define RELAY_1_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_2_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_3_TYPE  RELAY_TYPE_IMPULSE
#define RELAY_4_TYPE  RELAY_TYPE_NORMAL
#define RELAY_5_TYPE  RELAY_TYPE_NORMAL
#define RELAY_6_TYPE  RELAY_TYPE_NORMAL
#define RELAY_7_TYPE  RELAY_TYPE_NORMAL
#define RELAY_8_TYPE  RELAY_TYPE_NORMAL

#endif // HARDWARE_H
