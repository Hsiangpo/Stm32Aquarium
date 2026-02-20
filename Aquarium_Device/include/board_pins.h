#pragma once

#include "stm32f1xx_hal.h"

// Pin mapping follows `docs/Wiring_Guide.md` and NUCLEO-F103RB Arduino headers.

// === Sensors ===
// DS18B20 (1-Wire)
#define PIN_DS18B20_GPIO GPIOA
#define PIN_DS18B20_PIN  GPIO_PIN_8

// Analog sensors (ADC1)
#define ADC_PH_CHANNEL        ADC_CHANNEL_0 // A0 -> PA0
#define ADC_TDS_CHANNEL       ADC_CHANNEL_1 // A1 -> PA1
#define ADC_TURBIDITY_CHANNEL ADC_CHANNEL_4 // A2 -> PA4
#define ADC_WATER_LEVEL_CH    ADC_CHANNEL_8 // A3 -> PB0

// OLED (I2C1 remap to PB8/PB9)
#define PIN_OLED_I2C_SCL_GPIO GPIOB
#define PIN_OLED_I2C_SCL_PIN  GPIO_PIN_8
#define PIN_OLED_I2C_SDA_GPIO GPIOB
#define PIN_OLED_I2C_SDA_PIN  GPIO_PIN_9

// === Actuators ===
// Relays (active level depends on module; default code assumes "HIGH = ON")
#define PIN_RELAY_PUMP_IN_GPIO  GPIOB
#define PIN_RELAY_PUMP_IN_PIN   GPIO_PIN_5 // D4 -> PB5

// NOTE: PB4 is JTAG TRST by default. Firmware must disable JTAG (keep SWD) to use it as GPIO.
#define PIN_RELAY_PUMP_OUT_GPIO GPIOB
#define PIN_RELAY_PUMP_OUT_PIN  GPIO_PIN_4 // D5 -> PB4

#define PIN_RELAY_HEATER_GPIO   GPIOB
#define PIN_RELAY_HEATER_PIN    GPIO_PIN_10 // D6 -> PB10

// Buzzer + LED
#define PIN_BUZZER_GPIO GPIOA
#define PIN_BUZZER_PIN  GPIO_PIN_9 // D8 -> PA9

#define PIN_LED_GPIO GPIOA
#define PIN_LED_PIN  GPIO_PIN_5 // D13 -> PA5 (LD2)

// Servo PWM: D9 -> PC7, TIM3_CH2 full remap to PC6/7/8/9
#define PIN_SERVO_GPIO GPIOC
#define PIN_SERVO_PIN  GPIO_PIN_7

// === ESP32 AT (UART) ===
// Wiring guide uses USART2 on PA2/PA3 (Arduino D1/D0).
// On NUCLEO boards those pins may be tied to ST-LINK VCP by default; if you connect ESP32,
// detach the ST-LINK VCP jumpers to avoid bus contention.
#define ESP32_UART_INSTANCE USART2
#define PIN_ESP32_TX_GPIO   GPIOA
#define PIN_ESP32_TX_PIN    GPIO_PIN_2
#define PIN_ESP32_RX_GPIO   GPIOA
#define PIN_ESP32_RX_PIN    GPIO_PIN_3

// Optional reset pin (if wired): PC2
#define PIN_ESP32_RST_GPIO GPIOC
#define PIN_ESP32_RST_PIN  GPIO_PIN_2

