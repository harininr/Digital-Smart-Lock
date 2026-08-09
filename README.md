# Digital Smart Lock

A bare-metal embedded system project for the STM32F401CCU6 microcontroller that implements a Bluetooth-enabled OTP (One-Time Password) based digital smart lock. The system integrates various hardware modules including a Bluetooth transceiver, I2C LCD, 4x4 keypad, ultrasonic sensor, buzzer, and LEDs to provide a secure and interactive locking mechanism.

## Features

*   **Bluetooth Communication:** Connects with mobile devices via the HC-05 Bluetooth module to request and receive OTPs.
*   **OTP Verification:** Generates a random 4-digit OTP upon receiving the "GET OTP" command and verifies user input.
*   **Interactive UI:** Displays system status and prompts on an I2C-based 16x2 LCD display.
*   **Manual Input:** Reads the user-entered OTP through a 4x4 matrix keypad.
*   **Audio-Visual Feedback:** Utilizes LEDs and a buzzer to indicate successful unlocking, wrong OTP entry, and lockout states.
*   **Proximity Detection:** Includes an ultrasonic sensor for distance measurement (can be used to wake up the system when a user is nearby).

## Hardware Components

*   STM32F401CCU6 Microcontroller (Black Pill)
*   HC-05 Bluetooth Module
*   16x2 LCD Display with I2C Module
*   4x4 Matrix Keypad
*   HC-SR04 Ultrasonic Sensor
*   Buzzer
*   LEDs and Resistors

## Pin Configuration

| Component | Pin / Interface | Description |
| :--- | :--- | :--- |
| **HC-05 (Bluetooth)** | PA9 | USART1 TX (Connects to HC-05 RX) |
| | PA10 | USART1 RX (Connects to HC-05 TX) |
| **I2C LCD** | I2C1 (PB6/PB7) | Default Address: `0x27` (or `0x3F`) |
| **Keypad Rows** | PA6, PA7, PA11, PA8 | Row 1 to Row 4 |
| **Keypad Columns** | PA12, PA13, PA14, PA15 | Column 1 to Column 4 |
| **Ultrasonic Sensor** | PA0 | TRIG Pin (Output) |
| | PA1 | ECHO Pin (Input) |
| **Buzzer/LEDs** | Configurable | Refer to `Buzzer_Init` and `LED_Init` |

## Usage Instructions

1.  **Hardware Setup:** Connect all components to the STM32F401CCU6 as per the pin configuration.
2.  **Compilation & Flashing:** Compile the `Digital Smart Lock.c` source file using an ARM GCC toolchain or STM32CubeIDE. Flash the resulting binary to the microcontroller using ST-Link.
3.  **Power On:** Once powered, the LCD will display "Waiting..." followed by "Ready for BT".
4.  **Bluetooth Pairing:** Pair your mobile device with the HC-05 module and open a Bluetooth serial terminal app (Baud rate: 9600).
5.  **Request OTP:** Send the command `GET OTP` through the terminal. The STM32 will generate and send back a 4-digit OTP.
6.  **Enter OTP:** The LCD will prompt "Enter OTP:". Use the 4x4 keypad to type the 4-digit code.
7.  **Verification:** 
    *   **Success:** System unlocks, indicated by LCD, green LED, and a short buzzer beep.
    *   **Failure:** Incorrect OTP triggers a warning. Multiple incorrect attempts will result in a temporary lockout state with alarms.

## Code Structure

*   `main()`: System initialization (I2C, UART, Timers, GPIOs) and the main event loop for handling Bluetooth commands.
*   `process_line()`: Parses incoming USART commands and triggers OTP generation.
*   `generate_otp()` & `send_otp()`: Handles random OTP creation and transmission.
*   `Keypad_ReadOTP()` & `OTP_Check()`: Captures user input and validates it against the active OTP.
*   Peripheral Drivers: Functions like `USART1_Init`, `I2C1_LCD_Init`, `TIM2_Init`, etc., handle low-level register configurations.
