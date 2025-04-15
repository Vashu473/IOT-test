/*
ESP32-S3 DevKitC-1 Pin Configuration
====================================

This file contains pin definitions and configurations for the ESP32-S3-DevKitc-1-N16R8 board.
*/

#ifndef ESP32S3_PINS_H
#define ESP32S3_PINS_H

// GPIO Pins - Digital
#define PIN_D0 0
#define PIN_D1 1
#define PIN_D2 2
#define PIN_D3 3
#define PIN_D4 4
#define PIN_D5 5
#define PIN_D6 6
#define PIN_D7 7
#define PIN_D8 8
#define PIN_D9 9
#define PIN_D10 10
#define PIN_D11 11
#define PIN_D12 12
#define PIN_D13 13
#define PIN_D14 14
#define PIN_D15 15
#define PIN_D16 16
#define PIN_D17 17
#define PIN_D18 18
#define PIN_D19 19
#define PIN_D20 20
#define PIN_D21 21
#define PIN_D38 38
#define PIN_D39 39
#define PIN_D40 40
#define PIN_D41 41
#define PIN_D42 42
#define PIN_D43 43
#define PIN_D44 44
#define PIN_D45 45
#define PIN_D46 46
#define PIN_D47 47
#define PIN_D48 48

// GPIO Pins - Analog Inputs
#define PIN_ADC1 1
#define PIN_ADC2 2
#define PIN_ADC3 3
#define PIN_ADC4 4
#define PIN_ADC5 5
#define PIN_ADC6 6
#define PIN_ADC7 7
#define PIN_ADC8 8
#define PIN_ADC9 9
#define PIN_ADC10 10

// I2C Pins
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// SPI Pins
#define PIN_SPI_MOSI 11
#define PIN_SPI_MISO 13
#define PIN_SPI_SCK 12
#define PIN_SPI_SS 10

// UART Pins
#define PIN_UART_TX 43
#define PIN_UART_RX 44

// I2S Pins (for Microphone)
#define I2S_WS_PIN 42  // Word Select (LRCLK)
#define I2S_SD_PIN 41  // Serial Data (SD)
#define I2S_SCK_PIN 40 // Serial Clock (SCLK)

// LED Pin is already defined in pins_arduino.h as GPIO2
// DO NOT redefine LED_BUILTIN

// USB Pins
// #define PIN_USB_D +20 -- causing compile errors
// #define PIN_USB_D -19 -- causing compile errors

// Boot Button
#define PIN_BOOT 0 // Boot button is on GPIO0

#endif // ESP32S3_PINS_H