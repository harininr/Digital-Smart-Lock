/* stm32f401_hc05_otp.c
   Bare-metal example for STM32F401CCU6
   USART1: PA9 (TX) -> HC05 RX, PA10 (RX) <- HC05 TX
   Sends random 4-digit OTP via Bluetooth on command "GET OTP"
   Displays "Enter OTP:" on LCD
   Reads 4-digit OTP via 4x4 keypad (optional)
   Baud: 9600 @ 16 MHz (BRR = 0x0683)
*/

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#define LCD_ADDR 0x27    // Change to 0x3F if your module uses that
#define LCD_BACKLIGHT 0x08
#define ENABLE 0x04
#define RS 0x01
#define BUF_SIZE 64
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4
#define BUZZER_PIN 0
#define TRIG_PIN 0  // PA0
#define ECHO_PIN 1  // PA1


const char keymap[4][4] = {
    {'0','*','#','D'},
    {'8','7','9','C'},
    {'2','1','3','A'},
    {'5','4','6','B'}
};

const uint16_t row_pins[4] = {6, 7, 11, 8};  // PA6, PA7, PA11, PA8
const uint16_t col_pins[4] = {12, 13, 14, 15};
volatile uint8_t wrong_otp_count = 0;
volatile uint8_t lockout = 0;
volatile uint8_t countdown = 0;
uint16_t current_otp = 0;   // holds the active OTP
uint8_t otp_active = 0;     // flag to know if OTP is generated
volatile uint16_t otp_timer = 0;     // countdown for OTP validity
volatile uint8_t otp_timer_active = 0; // flag if OTP timer running
volatile uint8_t otp_entry_done = 0; // becomes 1 after last digit entered
// Ultrasonic sensor functions
/**
 * @brief Triggers the HC-SR04 ultrasonic sensor and calculates distance
 * @return Measured distance in centimeters
 */
uint32_t Measure_Distance(void);
uint8_t bt_connected = 0;  // 0 = not connected, 1 = connected





// Function prototypes
void delay_ms(volatile uint32_t ms);
void USART1_Init(void);
void USART1_SendChar(char c);
void USART1_SendString(const char *s);
char USART1_ReceiveCharBlocking(void);

/**
 * @brief Processes incoming string commands over Bluetooth (USART1)
 * @param buf Null-terminated string received via Bluetooth
 * Triggers OTP generation if 'GET OTP' is received.
 */
void process_line(char *buf);
/**
 * @brief Generates a pseudo-random 4-digit OTP
 * @return A 16-bit unsigned integer representing the 4-digit OTP (e.g., 1000 - 9999)
 */
uint16_t generate_otp(void);
/**
 * @brief Generates an OTP, starts the validity timer, and transmits it via Bluetooth
 * @return The generated OTP
 */
uint16_t send_otp(void);
/**
 * @brief Displays the 'Enter OTP:' prompt on the LCD screen
 */
void LCD_EnterOTP(void);
/**
 * @brief Scans the 4x4 matrix keypad to detect a pressed key
 * @return The character of the pressed key, or 0 if no key is pressed
 */
char Keypad_GetKey(void);
/**
 * @brief Reads a 4-digit OTP from the keypad and displays it on the LCD
 * @param otp_buffer Array to store the 4 entered characters plus null terminator
 */
void Keypad_ReadOTP(char *otp_buffer);
/**
 * @brief Initializes the GPIO pins for the status LEDs (Green/Red)
 */
void LED_Init(void);
/**
 * @brief Validates the entered OTP against the active OTP
 * @param otp The active generated OTP
 * @param entered The string of characters entered by the user
 * Unlocks the system if correct, or increments the wrong attempt counter.
 * Triggers a temporary lockout after 3 consecutive wrong attempts.
 */
void OTP_Check(uint16_t otp, char *entered);

// LCD/I2C functions
/**
 * @brief Initializes I2C1 peripheral for LCD communication
 */
void I2C1_LCD_Init(void);
void I2C1_LCD_Write(uint8_t data);
void LCD_SendNibble(uint8_t nibble, uint8_t mode);
void LCD_SendCommand(uint8_t cmd);
void LCD_SendData(uint8_t data);
/**
 * @brief Initializes the 16x2 I2C LCD with standard 4-bit mode configuration
 */
void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_String(char *str);
void Keypad_GPIO_Init(void);

void Buzzer_Init(void);
/**
 * @brief Initializes Timer 2 (TIM2) to generate 1-second interrupts
 * Used for tracking OTP expiry and lockout countdowns.
 */
void TIM2_Init(void);

/**
 * @brief Initializes ADC1 for analog sensor reading (e.g., LDR or Potentiometer)
 */
void ADC1_Init(void);
uint16_t ADC1_Read(void);

void LCD_PrintLine(uint8_t row, const char *str);




/**
 * @brief Main execution function for the Smart Lock
 * Initializes peripherals, sets up I2C, LCD, UART, and enters the main event loop
 * to process Bluetooth commands and check connection status.
 */
int main(void)
{
    char rxbuf[BUF_SIZE];
    uint32_t idx = 0;
	// PA0 -> TRIG output
		GPIOA->MODER &= ~(3<<(TRIG_PIN*2));
		GPIOA->MODER |=  (1<<(TRIG_PIN*2));

		// PA1 -> ECHO input
		GPIOA->MODER &= ~(3<<(ECHO_PIN*2));


    // Initialize I2C and LCD first
    I2C1_LCD_Init();
    LCD_Init();
	  Keypad_GPIO_Init();
		LED_Init();
		Buzzer_Init();
    TIM2_Init();
		ADC1_Init();
	
    // Initialize UART
    USART1_Init();

    for (int i = 0; i < BUF_SIZE; ++i) rxbuf[i] = 0;
		
		LCD_Clear();
		LCD_SetCursor(0,0);
		LCD_String("Waiting...");
		delay_ms(200);

		LCD_Clear();
		LCD_SetCursor(0,0);
		LCD_String("Ready for BT");
		delay_ms(1000); // short delay


    USART1_SendString("STM32 ready. Send 'GET OTP' to receive an OTP.\r\n");

    while (1)
    {
       if (USART1->SR & (1 << 5)) // RXNE
{
    char c = (char)(USART1->DR & 0xFF);
    USART1_SendChar(c); // echo back

    // Detect BT connection
    if(!bt_connected){
        bt_connected = 1;
        LCD_Clear();
        LCD_SetCursor(0,0);
        LCD_String("BT CONNECTED");
    }

    if (c != '\r' && c != '\n') {
        if (idx < (BUF_SIZE - 1))
            rxbuf[idx++] = c;
    } else {
        // End of command
        rxbuf[idx] = '\0';   // always null-terminate
        if (idx > 0) {
            process_line(rxbuf);
            idx = 0;
        }
        // Clear buffer for next command
        memset(rxbuf, 0, BUF_SIZE);
    }
}


}
}

void delay_ms(volatile uint32_t ms)
{
    while (ms--)
        for (volatile uint32_t i = 0; i < 12000; ++i)
            __asm("nop");
}

// Handle received commands
void process_line(char *buf)
{
    // Trim leading spaces
    while (*buf == ' ') buf++;

    // Trim trailing newline or carriage return
    size_t len = strlen(buf);
    while(len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n'))
    {
        buf[len-1] = '\0';
        len--;
    }

    if(strcmp(buf, "GET OTP")==0){
        uint16_t otp = send_otp();      // generate & display OTP
        char entered[5];
        Keypad_ReadOTP(entered);        // read from keypad
        OTP_Check(otp, entered);        // validate and show LED/LCD
    }
    else{
        USART1_SendString("\r\nUnknown command\r\n");
    }
}



uint16_t generate_otp(void)
{
    static uint8_t initialized = 0;
    static uint32_t seed;
    if(!initialized){
        seed = TIM2->CNT;  // read current timer value as random seed
        initialized = 1;
    }

    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return (seed % 9000) + 1000;
}


// Send OTP via Bluetooth and display prompt on LCD
uint16_t send_otp(void)
{
    if (otp_active) {
        USART1_SendString("\r\nOTP already active. Use same OTP.\r\n");
        
        // Clear LCD fully
        LCD_Clear();

        // Line 0: OTP input (empty for now)
        LCD_SetCursor(0, 0);
        LCD_String("OTP:   "); // space to clear previous digits

        // Line 1: Countdown
        LCD_SetCursor(1, 0);
        char msg[32];
        snprintf(msg, sizeof(msg), "EXPIRES IN: %2ds", otp_timer);
        LCD_PrintLine(1, msg);

        return current_otp;
    }

    // Generate new OTP
    current_otp = generate_otp();
    otp_active = 1;
		otp_entry_done = 0; 

    // Get validity from potentiometer
    uint16_t adc_val = ADC1_Read();
    uint8_t validity_time = 60 + (adc_val * 50 / 4095);
    otp_timer = validity_time;
    otp_timer_active = 1;

    // Send OTP over USART
    char msg[32];
    sprintf(msg, "%04d", current_otp);
    USART1_SendString("\r\nYour OTP is: ");
    USART1_SendString(msg);
    USART1_SendString("\r\n");

    // LCD: clear and display
    LCD_Clear();

    // Line 0: OTP input placeholder
    LCD_SetCursor(0, 0);
    LCD_String("OTP:        "); // space to hold 4 digits

    // Line 1: Countdown
    LCD_SetCursor(1, 0);
    snprintf(msg, sizeof(msg), "EXPIRES IN: %2ds", validity_time);
    LCD_PrintLine(1, msg);

    TIM2->CR1 |= TIM_CR1_CEN; // start timer

    return current_otp;
}



// USART1 initialization
void USART1_Init(void)
{
    RCC->APB2ENR |= (1 << 4);
    RCC->AHB1ENR |= (1 << 0);

    GPIOA->MODER &= ~((3U << (9*2)) | (3U << (10*2)));
    GPIOA->MODER |= ((2U << (9*2)) | (2U << (10*2)));

    GPIOA->AFR[1] &= ~((0xF << ((9-8)*4)) | (0xF << ((10-8)*4)));
    GPIOA->AFR[1] |= ((7 << ((9-8)*4)) | (7 << ((10-8)*4)));

    USART1->BRR = 0x0683;
    USART1->CR1 = (1<<13) | (1<<3) | (1<<2);
}

void USART1_SendChar(char c)
{
    while (!(USART1->SR & (1<<7)));
    USART1->DR = (uint8_t)c;
}

void USART1_SendString(const char *s)
{
    while (*s) USART1_SendChar(*s++);
}

/* ------------------- LCD / I2C ------------------- */
void I2C1_LCD_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    GPIOB->MODER &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->MODER |= ((2U << (6*2)) | (2U << (7*2)));
    GPIOB->OTYPER |= (1<<6) | (1<<7);
    GPIOB->AFR[0] |= (4<< (6*4)) | (4<< (7*4));

    I2C1->CR1 = 0;
    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;
    I2C1->CR1 |= I2C_CR1_PE;
}

void I2C1_LCD_Write(uint8_t data)
{
    while(I2C1->SR2 & I2C_SR2_BUSY);
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));
    I2C1->DR = (LCD_ADDR << 1);
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2;
    while(!(I2C1->SR1 & I2C_SR1_TXE));
    I2C1->DR = data;
    while(!(I2C1->SR1 & I2C_SR1_BTF));
    I2C1->CR1 |= I2C_CR1_STOP;
}

void LCD_SendNibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data = (nibble & 0xF0) | LCD_BACKLIGHT | mode;
    I2C1_LCD_Write(data);          // put nibble on bus
    I2C1_LCD_Write(data | ENABLE); // EN high
    // tiny delay
    for(volatile int i=0;i<50;i++); 
    I2C1_LCD_Write(data & ~ENABLE); // EN low
    for(volatile int i=0;i<50;i++); 
}



void LCD_SendCommand(uint8_t cmd)
{
    LCD_SendNibble(cmd & 0xF0, 0);
    LCD_SendNibble((cmd << 4) & 0xF0, 0);
    delay_ms(2);
}

void LCD_SendData(uint8_t data)
{
    LCD_SendNibble(data & 0xF0, RS);
    LCD_SendNibble((data << 4) & 0xF0, RS);
    delay_ms(2);
}

void LCD_Init(void)
{
    delay_ms(50);
    LCD_SendNibble(0x30,0);
    delay_ms(5);
    LCD_SendNibble(0x30,0);
    delay_ms(1);
    LCD_SendNibble(0x30,0);
    LCD_SendNibble(0x20,0);

    LCD_SendCommand(0x28);
    LCD_SendCommand(0x0C);
    LCD_SendCommand(0x06);
    LCD_SendCommand(0x01);
    delay_ms(5);
	
	// PA4 & PA5 as LEDs output
GPIOA->MODER &= ~((3<<4*2) | (3<<5*2) | (3<<BUZZER_PIN*2));
GPIOA->MODER |=  ((1<<4*2) | (1<<5*2) | (1<<BUZZER_PIN*2));

}

void LCD_Clear(void)
{
    LCD_SendCommand(0x01);
    delay_ms(3);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr[] = {0x80, 0xC0};
    LCD_SendCommand(addr[row] + col);
}

void LCD_String(char *str)
{
    while(*str) LCD_SendData(*str++);
}

void LCD_EnterOTP(void)
{
    LCD_Clear();
    LCD_SetCursor(0,0);
    LCD_String("Enter OTP:");
}

/* ------------------- KEYPAD ------------------- */
char Keypad_GetKey(void)
{
    uint16_t row_pins[4] = {6,7,8,11};
    uint16_t col_pins[4] = {12,13,14,15};

    while(1) // wait until a key is pressed
    {
        for(int r=0;r<4;r++)
        {
            // Set all rows LOW
            GPIOA->ODR &= ~((1<<6)|(1<<7)|(1<<8)|(1<<11));
            GPIOA->ODR |= (1<<row_pins[r]); // Activate current row

            for(volatile int d=0; d<2000; d++); // small debounce

            for(int c=0;c<4;c++)
            {
                if(GPIOB->IDR & (1<<col_pins[c]))
                {
                    while(GPIOB->IDR & (1<<col_pins[c])); // wait release
                    char key = keymap[r][c];
                    if(key >= '0' && key <= '9') // only accept digits
                        return key;
                }
            }
        }
    }
}


void Keypad_GPIO_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    // Rows PA6,7,8,11 as OUTPUT
    GPIOA->MODER &= ~((3<<6*2)|(3<<7*2)|(3<<8*2)|(3<<11*2));
    GPIOA->MODER |=  ((1<<6*2)|(1<<7*2)|(1<<8*2)|(1<<11*2));

    // Columns PB12–PB15 as INPUT with pull-down
    GPIOB->MODER &= ~((3<<12*2)|(3<<13*2)|(3<<14*2)|(3<<15*2));
    GPIOB->PUPDR &= ~((3<<12*2)|(3<<13*2)|(3<<14*2)|(3<<15*2));
    GPIOB->PUPDR |=  ((2<<12*2)|(2<<13*2)|(2<<14*2)|(2<<15*2));

    // PB6,PB7 -> I2C1 SDA/SCL AF4
    GPIOB->MODER &= ~(3<<(6*2)|3<<(7*2));
    GPIOB->MODER |=  (2<<(6*2)|2<<(7*2));
    GPIOB->AFR[0] |= (4<<(6*4)) | (4<<(7*4));
    GPIOB->OTYPER |= (1<<6)|(1<<7);
    GPIOB->PUPDR |= (1<<6*2)|(1<<7*2);
}


void Keypad_ReadOTP(char *otp_buffer)
{
    LCD_SetCursor(0, 0);
    LCD_String("OTP:    ");  // 4 spaces for digits

    for(int i = 0; i < 4; i++)
    {
        char key = Keypad_GetKey();
        otp_buffer[i] = key;

        LCD_SetCursor(0, 5 + i); // after "OTP: "
        LCD_SendData(key);

        delay_ms(20);
    }

    otp_buffer[4] = '\0';

    // Stop showing countdown now
    otp_entry_done = 1;
}





void LED_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // PA4 & PA5 as LEDs output, PA6 as buzzer output
    GPIOA->MODER &= ~((3<<4*2) | (3<<5*2) | (3<<BUZZER_PIN*2));
    GPIOA->MODER |=  ((1<<4*2) | (1<<5*2) | (1<<BUZZER_PIN*2));

    // Initially OFF
    GPIOA->ODR &= ~((1<<4) | (1<<5) | (1<<BUZZER_PIN));
}


void TIM2_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; // Enable TIM2 clock
    TIM2->PSC = 16000 - 1;               // Prescaler for 1ms tick (16 MHz / 16000 = 1 kHz)
    TIM2->ARR = 1000 - 1;                // 1000 ms = 1 second
    TIM2->DIER |= TIM_DIER_UIE;          // Enable update interrupt
    TIM2->CR1 |= TIM_CR1_CEN;            // Start timer

    NVIC_EnableIRQ(TIM2_IRQn);           // Enable TIM2 interrupt in NVIC
}


/**
 * @brief Interrupt Handler for Timer 2
 * Manages the lockout countdown and OTP expiry timer.
 */
void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF)
    {
        TIM2->SR &= ~TIM_SR_UIF;  // clear interrupt

        if (lockout)
        {
            if (countdown > 0)
            {
                countdown--;
                LCD_SetCursor(0,0);
                char msg[32];
                sprintf(msg, "LOCKED: %ds  ", countdown); // trailing spaces to clear
                LCD_String(msg);
            }
            else
            {
                lockout = 0;
                wrong_otp_count = 0;
                GPIOA->ODR &= ~(1<<5);
                GPIOB->ODR &= ~(1<<0);
                LCD_Clear();
                LCD_SetCursor(0,0);
                LCD_String("EXPIRES IN: 0s");
                LCD_SetCursor(1,0);
                LCD_String("ENTER OTP:");
                TIM2->CR1 &= ~TIM_CR1_CEN; // stop timer if not needed
            }
        }
       else if (otp_timer_active)
{
    if (otp_timer_active && otp_timer > 0)
    {
        otp_timer--;
        LCD_SetCursor(1,0);   // only line 1
        char msg[32];
        snprintf(msg, sizeof(msg), "EXPIRES IN: %2ds  ", otp_timer); // overwrite previous
        LCD_String(msg);
    }
    else
    {
        // OTP expired
        otp_active = 0;
        otp_timer_active = 0;
        current_otp = 0;

        LCD_Clear();
        LCD_SetCursor(0,0);
        LCD_String("OTP Expired!");
        LCD_SetCursor(1,0);
        LCD_String("GENERATE AGAIN");

        USART1_SendString("\r\nOTP expired. Please generate again.\r\n");
        TIM2->CR1 &= ~TIM_CR1_CEN; // stop timer
    }
}
}}

void Buzzer_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER &= ~(3<<0*2);
    GPIOB->MODER |=  (1<<0*2); // PB0 output
    GPIOB->ODR &= ~(1<<0);     // Initially OFF
}


#include <string.h>

void OTP_Check(uint16_t otp, char *entered)
{	
    if (!otp_active) return;

    char otp_str[5];
    sprintf(otp_str, "%04d", otp);

    if(strcmp(entered, otp_str) == 0)
    {
        // Correct OTP
        GPIOA->ODR |= (1 << 4);   // green LED
        GPIOA->ODR &= ~(1 << 5);  // red LED
        GPIOB->ODR &= ~(1 << 0);  // buzzer OFF

        LCD_Clear();
        LCD_SetCursor(0, 0);
        LCD_String("VALID OTP      ");
        LCD_SetCursor(1, 0);
        LCD_String("                ");

        otp_active = 0;
        otp_timer_active = 0;
        TIM2->CR1 &= ~TIM_CR1_CEN; // stop timer
        wrong_otp_count = 0;

        USART1_SendString("\r\nOTP VALID\r\n");
    }
 else
{
    // Wrong OTP
    wrong_otp_count++;
    GPIOA->ODR |= (1 << 5);   // red LED
    GPIOA->ODR &= ~(1 << 4);  // green LED

if(wrong_otp_count < 3)
{
    uint8_t attempts_left = 3 - wrong_otp_count;
    char msg[32];

    // Show TRY AGAIN message briefly
    snprintf(msg, sizeof(msg), "TRY AGAIN:      %d", attempts_left);
    LCD_Clear();
    LCD_SetCursor(0,0);
    LCD_String(msg);

    USART1_SendString("\r\n");
    USART1_SendString(msg);
    USART1_SendString("\r\n");

    delay_ms(100); // 2s display

    // Show OTP entry again
    LCD_Clear();
    LCD_SetCursor(0, 0);
    LCD_String("ENTER OTP:");

    // Optional: show 4 spaces for digits
    LCD_SetCursor(0, 10);
    LCD_String("          "); 

    otp_entry_done = 0; // reset entry flag
    // Do NOT call OTP_Check here. Let main loop read new OTP
		char new_entry[32];
        Keypad_ReadOTP(new_entry);

        // Validate the newly entered OTP
        OTP_Check(current_otp, new_entry);
}

        else
        {
            // Lockout after 3 wrong attempts
            lockout = 1;
            countdown = 5; // 5 seconds timeout
            GPIOA->ODR |= (1 << 5);  // red LED
            GPIOA->ODR &= ~(1 << 4);
            GPIOB->ODR |= (1 << 0);  // buzzer ON
            TIM2->CR1 |= TIM_CR1_CEN;

            LCD_Clear();
            LCD_SetCursor(0, 0);
            LCD_String("LOCKED! 5s");

            USART1_SendString("\r\n3 Wrong Attempts. Locked for 5s\r\n");

            otp_active = 0;
            otp_timer_active = 0;
            wrong_otp_count = 0;
        }
    }
}


    

void ADC1_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // PA2 as analog
    GPIOA->MODER |= (3 << (2 * 2));
    GPIOA->PUPDR &= ~(3 << (2 * 2));

    ADC1->SQR3 = 2; // channel 2
    ADC1->SMPR2 |= (7 << (3 * 2)); // sample time
    ADC1->CR2 |= ADC_CR2_ADON;
}

uint16_t ADC1_Read(void)
{
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)ADC1->DR;
}


void LCD_PrintLine(uint8_t row, const char *str)
{
    LCD_SetCursor(row, 0); // move cursor to start
    for(int i=0;i<16;i++)
    {
        if(str[i] != '\0')
            LCD_SendData(str[i]);
        else
            LCD_SendData(' '); // pad to clear
    }
}

void I2C_LCD_Write(uint8_t data)
{
    while(I2C1->SR2 & I2C_SR2_BUSY);       // wait if busy
    I2C1->CR1 |= I2C_CR1_START;           // start condition
    while(!(I2C1->SR1 & I2C_SR1_SB));     // wait for start
    I2C1->DR = (LCD_ADDR << 1);           // send address + write
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2;                       // clear ADDR flag
    while(!(I2C1->SR1 & I2C_SR1_TXE));     // wait TXE
    I2C1->DR = data;
    while(!(I2C1->SR1 & I2C_SR1_BTF));    // wait BTF
    I2C1->CR1 |= I2C_CR1_STOP;
    delay_ms(1);                           // allow LCD to process
}


void delay_us(uint32_t us){
    volatile uint32_t n;
    while(us--){
        n = 16;
        while(n--);
    }
}

uint32_t Measure_Distance(void){
    uint32_t count = 0;
    uint32_t timeout = 60000;

    GPIOA->ODR &= ~(1<<TRIG_PIN);
    delay_us(2);
    GPIOA->ODR |= (1<<TRIG_PIN);
    delay_us(10);
    GPIOA->ODR &= ~(1<<TRIG_PIN);

    while(!(GPIOA->IDR & (1<<ECHO_PIN))){
        if(timeout-- == 0) return 0;
    }

    count = 0;
    timeout = 60000;
    while(GPIOA->IDR & (1<<ECHO_PIN)){
        count++;
        delay_us(1);
        if(timeout-- == 0) break;
    }

    return count / 58; 
}
