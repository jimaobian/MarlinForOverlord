#include "Configuration.h"
#include "pins.h"
#include "UltiLCD2_low_lib.h"
#include <compat/twi.h>


#ifdef ENABLE_ULTILCD2
/**
 * Implementation of the LCD display routines for a SSD1309 OLED graphical display connected with i2c.
 **/
#define LCD_GFX_WIDTH 128
#define LCD_GFX_HEIGHT 64

#define LCD_RESET_PIN 5
//#define LCD_CS_PIN    6
#define I2C_SDA_PIN   20
#define I2C_SCL_PIN   21

#define I2C_FREQ 400000

//The TWI interrupt routine conflicts with an interrupt already defined by Arduino, if you are using the Arduino IDE.
// Not running the screen update from interrupts causes a 25ms delay each screen refresh. Which will cause issues during printing.
// I recommend against using the Arduino IDE and setup a proper development environment.
#define USE_TWI_INTERRUPT 1

#define I2C_WRITE   0x00
#define I2C_READ    0x01

#define I2C_LED_ADDRESS 0b1100000

#define I2C_LCD_ADDRESS 0b0111100
#define I2C_LCD_SEND_COMMAND 0x00
#define I2C_LCD_SEND_DATA    0x40

#define LCD_COMMAND_CONTRAST                0x81
#define LCD_COMMAND_FULL_DISPLAY_ON_DISABLE 0xA4
#define LCD_COMMAND_FULL_DISPLAY_ON_ENABLE  0xA5
#define LCD_COMMAND_INVERT_DISABLE          0xA6
#define LCD_COMMAND_INVERT_ENABLE           0xA7
#define LCD_COMMAND_DISPLAY_OFF             0xAE
#define LCD_COMMAND_DISPLAY_ON              0xAF
#define LCD_COMMAND_NOP                     0xE3
#define LCD_COMMAND_LOCK_COMMANDS           0xDF

#define LCD_COMMAND_SET_ADDRESSING_MODE     0x20

/** Backbuffer for LCD */
uint8_t lcd_buffer[LCD_GFX_WIDTH * LCD_GFX_HEIGHT / 8];
uint8_t led_r, led_g, led_b;

#define LcdUpdateNormal 0
#define LcdUpdateError 1
#define LcdUpdateWaiting 2
#define LcdUpdateErrorInInterrupt 3

//boolean iicdebug=false;
static volatile uint8_t lcd_update_state=LcdUpdateNormal;
static volatile uint16_t lcd_update_pos = 0;

#define beepTypeNone   0
#define beepTypePress   1
#define beepTypeNormal    2

uint8_t beepType=beepTypeNone;


FORCE_INLINE static void i2c_end()
{
    uint8_t i2cTimeOut=0;
    TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);
    while (TWCR&(1<<TWSTO)){
        if (i2cTimeOut++==200) {
            SERIAL_DEBUGLNPGM("i2c TimeOut");
            lcd_update_state=LcdUpdateError;
            TWCR=0;
            break;
        }
    }
}

FORCE_INLINE static void i2c_end_normal()
{
    uint8_t i2cTimeOut=0;
    if ((TWSR & 0xF8 )== TW_MT_DATA_ACK) {
        TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);
        while (TWCR&(1<<TWSTO)){
            if (i2cTimeOut++==200) {
                SERIAL_DEBUGLNPGM("i2c TimeOut");
                lcd_update_state=LcdUpdateError;
                TWCR=0;
                break;
            }
        }
    }
    else{
        lcd_update_state=LcdUpdateError;
        SERIAL_DEBUGLNPGM("not normal end");
        SERIAL_DEBUGLN(TWSR & 0xF8);
    }
}


uint8_t lcdVersion=0;

FORCE_INLINE static boolean i2c_start_address(uint8_t data)
{
    uint8_t i2cTimeOut=0;
    
    TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);
    i2cTimeOut=0;
    while (!(TWCR & (1<<TWINT))){
        if (i2cTimeOut++==200) {
            SERIAL_DEBUGLNPGM("i2c TimeOut");
            lcd_update_state=LcdUpdateError;
            TWCR=0;
            break;
        }
    }
    switch (TWSR & 0xF8) {
        case TW_REP_START:
            SERIAL_DEBUGLNPGM("i2c start TW_REP_START");
        case TW_START:
        {
            TWDR = data;
            TWCR = (1<<TWINT) | (1<<TWEN);
            i2cTimeOut=0;
            while (!(TWCR & (1<<TWINT))){
                if (i2cTimeOut++==200) {
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("i2c TimeOut");
                    SERIAL_DEBUGLNPGM("TW_START");
                    TWCR=0;
                    break;
                }
            }
            switch (TWSR & 0xF8) {
                case TW_MT_SLA_ACK:
                    return true;
                    break;
                case TW_MT_SLA_NACK:
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("iic address error");
                    SERIAL_DEBUGLNPGM("TW_MT_SLA_NACK");
                    i2c_end();
                    break;
                case TW_BUS_ERROR:
                {
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("iic address error");
                    SERIAL_DEBUGLNPGM("TW_BUS_ERROR");
                    SERIAL_DEBUGLN(TWSR & 0xF8);
                    i2c_end();
                    return false;
                }
                    break;
                    
                case TW_MR_SLA_ACK:
                    SERIAL_DEBUGLNPGM("into TW_MR_SLA_ACK");
                    
                    TWCR = (1<<TWINT) | (1<<TWEN);
                    i2cTimeOut=0;
                    while (!(TWCR & (1<<TWINT))){
                        if (i2cTimeOut++==200) {
                            lcd_update_state=LcdUpdateError;
                            SERIAL_DEBUGLNPGM("i2c TimeOut");
                            SERIAL_DEBUGLNPGM("TW_MR_SLA_ACK");
                            TWCR=0;
                            break;
                        }
                    }
                    
                    if ((TWSR & 0xF8) == TW_MR_DATA_NACK) {
                        lcdVersion=TWDR;
                        SERIAL_DEBUGLNPGM("i2c lcdVersion");
                        SERIAL_DEBUGLN((int)lcdVersion);
                        return true;
                    }
                    else{
                        lcd_update_state=LcdUpdateError;
                        SERIAL_DEBUGLNPGM("i2c TW_MR_DATA_NACK");
                    }
                    break;
                    
                case TW_MR_SLA_NACK:
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("iic address error");
                    SERIAL_DEBUGLNPGM("TW_MR_SLA_NACK");
                    i2c_end();
                    break;
                    
                case TW_MR_ARB_LOST:
                {
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("iic address error");
                    SERIAL_DEBUGLNPGM("TW_MR_ARB_LOST");
                    SERIAL_DEBUGLN(TWSR & 0xF8);
                    TWCR = (1<<TWINT)|(1<<TWEN);
                    return false;
                    break;
                }
                default:
                    lcd_update_state=LcdUpdateError;
                    SERIAL_DEBUGLNPGM("iic address error");
                    SERIAL_DEBUGLNPGM("default");
                    SERIAL_DEBUGLN(TWSR & 0xF8);
                    break;
            }
        }
            break;
        case TW_BUS_ERROR:
        {
            SERIAL_DEBUGLNPGM("iic start error");
            SERIAL_DEBUGLNPGM("TW_BUS_ERROR");
            lcd_update_state=LcdUpdateError;
            i2c_end();
            return false;
        }
            break;
        case TW_MR_ARB_LOST:
        {
            SERIAL_DEBUGLNPGM("iic start error");
            SERIAL_DEBUGLNPGM("TW_MR_ARB_LOST");
            lcd_update_state=LcdUpdateError;
            TWCR = (1<<TWINT)|(1<<TWEN);
            return false;
        }
            break;
        default:
            lcd_update_state=LcdUpdateError;
            SERIAL_DEBUGLNPGM("iic start error");
            SERIAL_DEBUGLNPGM("default");
            SERIAL_DEBUGLN(TWSR & 0xF8);
            break;
    }
    lcd_update_state=LcdUpdateError;
    return false;
}





FORCE_INLINE static void i2c_send_data(uint8_t data)
{
    uint8_t i2cTimeOut=0;
    TWDR = data;
    TWCR = (1<<TWINT) | (1<<TWEN);
    i2cTimeOut=0;
    while (!(TWCR & (1<<TWINT))){
        if (i2cTimeOut++==200) {
            SERIAL_DEBUGLNPGM("i2cTimeOut");
            SERIAL_DEBUGLNPGM("data");
            TWCR=0;
            lcd_update_state=LcdUpdateError;
            break;
        }
    }
    switch (TWSR & 0xF8) {
        case TW_MT_DATA_ACK:
            break;
        case TW_MR_ARB_LOST:
            SERIAL_DEBUGLNPGM("iic data error");
            SERIAL_DEBUGLNPGM("TW_MR_ARB_LOST");
            SERIAL_DEBUGLN(TWSR & 0xF8);
            TWCR = (1<<TWINT)|(1<<TWEN);
            lcd_update_state=LcdUpdateError;
            break;
        default:
            SERIAL_DEBUGLNPGM("iic data error");
            SERIAL_DEBUGLNPGM("default");
            SERIAL_DEBUGLN(TWSR & 0xF8);
            i2c_end();
            lcd_update_state=LcdUpdateError;
            break;
    }
}

//void lcd_lib_set_contrast(uint8_t theContrast)
//{
//    if (!lcd_lib_update_ready()) return;
//    
//    uint16_t extendContrast=(uint16_t)theContrast+(uint16_t)theContrast/2;
//    
//        i2c_start();
//        i2c_send_address(I2C_LCD_ADDRESS << 1 | I2C_WRITE);
//        i2c_send_data(I2C_LCD_SEND_COMMAND);
//    if (extendContrast>335) {
//        i2c_send_data(LCD_COMMAND_CONTRAST);
//        i2c_send_data(0xFF);
//        i2c_send_data(0xDB);
//        i2c_send_data(0x3C);
//    }
//    else if (extendContrast>110) {
//        i2c_send_data(LCD_COMMAND_CONTRAST);
//        i2c_send_data(extendContrast-110);
//        i2c_send_data(0xDB);
//        i2c_send_data(0x3C);
//    }
//    else if (extendContrast>75){
//        i2c_send_data(LCD_COMMAND_CONTRAST);
//        i2c_send_data(extendContrast-75);
//        i2c_send_data(0xDB);
//        i2c_send_data(0x34);
//    }
//    else{
//        i2c_send_data(LCD_COMMAND_CONTRAST);
//        i2c_send_data(extendContrast);
//        i2c_send_data(0xDB);
//        i2c_send_data(0x00);
//    }
//        i2c_end();
//}


void lcd_lib_oled_reset(){
    WRITE(LCD_RESET_PIN, 0);
    _delay_ms(5);
    WRITE(LCD_RESET_PIN, 1);
    _delay_ms(5);
    
    TWBR = ((F_CPU / I2C_FREQ) - 16)/2*1;
    TWSR = 0x00;
    
    
    if (i2c_start_address((I2C_LED_ADDRESS << 1) | I2C_READ)) {
        SERIAL_DEBUGLN("i2c read success");
        i2c_end();
    }
    
    delayMicroseconds(100);
    
    if (i2c_start_address((I2C_LED_ADDRESS << 1) | I2C_READ)) {
        SERIAL_DEBUGLN("i2c read success");
        i2c_end();
    }
    
    delayMicroseconds(100);
    
    if (i2c_start_address((I2C_LCD_ADDRESS << 1) | I2C_WRITE)) {
        i2c_send_data(I2C_LCD_SEND_COMMAND);
        
        i2c_send_data(LCD_COMMAND_LOCK_COMMANDS);
        i2c_send_data(0x12);
        
        i2c_send_data(LCD_COMMAND_DISPLAY_OFF);
        
        i2c_send_data(0xD5);//Display clock divider/freq
        //    i2c_send_raw(0xA0);
        i2c_send_data(0xF0);
        
        i2c_send_data(0xA8);//Multiplex ratio
        i2c_send_data(0x3F);
        
        i2c_send_data(0xD3);//Display offset
        i2c_send_data(0x00);
        
        i2c_send_data(0x40);//Set start line
        
        i2c_send_data(0xA1);//Segment remap
        
        i2c_send_data(0xC8);//COM scan output direction
        
        i2c_send_data(0xDA);//COM pins hardware configuration
        i2c_send_data(0x12);
        
        i2c_send_data(LCD_COMMAND_CONTRAST);
        //    i2c_send_raw(0xDF);
        i2c_send_data(0xFF);
        
        i2c_send_data(0xD9);//Pre charge period
        //    i2c_send_raw(0x82);
        i2c_send_data(0xF1);
        
        
        i2c_send_data(0xDB);//VCOMH Deslect level
        //    i2c_send_raw(0x34);
        i2c_send_data(0x3C);
        
        i2c_send_data(LCD_COMMAND_SET_ADDRESSING_MODE);
        i2c_send_data(0x00);
        
        i2c_send_data(0x22);
        i2c_send_data(0x00);
        i2c_send_data(0x07);
        
        if (lcdVersion == 10) {
            i2c_send_data(0x21);
            i2c_send_data(0x02);
            i2c_send_data(0x81);
        }
        else{
            i2c_send_data(0x21);
            i2c_send_data(0x00);
            i2c_send_data(0x7f);
        }
        
        i2c_send_data(LCD_COMMAND_FULL_DISPLAY_ON_DISABLE);
        
        i2c_send_data(LCD_COMMAND_DISPLAY_ON);
        
        i2c_end_normal();
    }
}

void lcd_lib_init()
{
//    SET_OUTPUT(LCD_CS_PIN);
    SET_OUTPUT(LCD_RESET_PIN);

    SET_OUTPUT(I2C_SDA_PIN);
    SET_OUTPUT(I2C_SCL_PIN);

    //Set the beeper as output.
//    SET_OUTPUT(BEEPER);
  
    //Set the encoder bits and encoder button as inputs with pullup
#ifdef PushButton
  SET_INPUT(PushButtonUp);
  SET_INPUT(PushButtonEnter);
  SET_INPUT(PushButtonDown);
  WRITE(PushButtonUp, 1);
  WRITE(PushButtonEnter, 1);
  WRITE(PushButtonDown, 1);
#else
  
    SET_INPUT(BTN_EN1);
    SET_INPUT(BTN_EN2);
    SET_INPUT(BTN_ENC);
    WRITE(BTN_EN1, 1);
    WRITE(BTN_EN2, 1);
    WRITE(BTN_ENC, 1);
#endif

    SET_INPUT(SDCARDDETECT);
    WRITE(SDCARDDETECT, HIGH);

//    WRITE(LCD_CS_PIN, 0);
    WRITE(I2C_SDA_PIN, 1);
    WRITE(I2C_SCL_PIN, 1);

    lcd_lib_oled_reset();

    lcd_lib_buttons_update_interrupt();
    lcd_lib_buttons_update();
    lcd_lib_encoder_pos = 0;
    lcd_lib_update_screen();
}

#if USE_TWI_INTERRUPT
ISR(TWI_vect)
{
    
    switch (TWSR & 0xF8) {
        case TW_MT_DATA_ACK:
            if (lcd_update_pos == LCD_GFX_WIDTH*LCD_GFX_HEIGHT/8)
            {
                lcd_update_pos=0;
                i2c_end();
            }else{
                
                TWDR = lcd_buffer[lcd_update_pos];
                TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWIE);
                lcd_update_pos++;
            }
            break;
        case TW_MR_ARB_LOST:
            lcd_update_state=LcdUpdateErrorInInterrupt;
            TWCR = (1<<TWINT)|(1<<TWEN);
            break;
        default:
            lcd_update_state=LcdUpdateErrorInInterrupt;
            i2c_end();
            break;
    }
}
#endif

void lcd_lib_update_screen()
{
    static unsigned long lcdUpdatePosDelayTimer;
    
    if (lcd_update_state==LcdUpdateError) {
        SERIAL_DEBUGLNPGM("LcdUpdateError");
        lcd_update_state=LcdUpdateWaiting;
        
        lcdUpdatePosDelayTimer=millis();
    }
    
    if (lcd_update_state==LcdUpdateErrorInInterrupt) {
        SERIAL_DEBUGLNPGM("LcdUpdateErrorInInterrupt");
        lcd_update_state=LcdUpdateWaiting;
        
        lcdUpdatePosDelayTimer=millis();
    }
  
    
    if (lcd_update_state==LcdUpdateWaiting && millis()-lcdUpdatePosDelayTimer>500) {
        SERIAL_DEBUGLNPGM("LcdUpdateWaiting");
        
        lcd_lib_oled_reset();
        
        if (lcd_update_state==LcdUpdateWaiting) {
            lcd_update_pos=0;
            lcd_update_state=LcdUpdateNormal;
            SERIAL_DEBUGLNPGM("LcdUpdateNormal");
        }
        
    }
    
    
    if (lcd_update_state==LcdUpdateNormal) {
        
        if (i2c_start_address((I2C_LED_ADDRESS << 1) | I2C_WRITE)) {
            i2c_send_data(2);
            i2c_send_data(led_g);
            i2c_send_data(3);
            i2c_send_data(led_r);
            i2c_send_data(4);
            i2c_send_data(led_b);

          switch (beepType) {
            case beepTypeNormal:
              i2c_send_data(9);
              i2c_send_data(0);
              beepType=beepTypeNone;
              break;
            case beepTypePress:
              i2c_send_data(9);
              i2c_send_data(2);
              beepType=beepTypeNone;
              break;
            default:
              i2c_send_data(9);
              i2c_send_data(3);
              beepType=beepTypeNone;
              break;
          }
            i2c_end_normal();
        }
        
        if (i2c_start_address((I2C_LCD_ADDRESS << 1) | I2C_WRITE)) {
            TWDR = I2C_LCD_SEND_DATA;
            TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWIE);
        }
    }
}

bool lcd_lib_update_ready()
{
//#if USE_TWI_INTERRUPT
    return !(TWCR & _BV(TWIE));
//#else
//    return true;
//#endif
}

void lcd_lib_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    led_r = r;
    led_g = g;
    led_b = b;
}

static const uint8_t lcd_font_7x5[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00,// (space)
	0x00, 0x00, 0x5F, 0x00, 0x00,// !
	0x00, 0x07, 0x00, 0x07, 0x00,// "
	0x14, 0x7F, 0x14, 0x7F, 0x14,// #
	0x24, 0x2A, 0x7F, 0x2A, 0x12,// $
	0x23, 0x13, 0x08, 0x64, 0x62,// %
	0x36, 0x49, 0x55, 0x22, 0x50,// &
	0x00, 0x05, 0x03, 0x00, 0x00,// '
	0x00, 0x1C, 0x22, 0x41, 0x00,// (
	0x00, 0x41, 0x22, 0x1C, 0x00,// )
	0x08, 0x2A, 0x1C, 0x2A, 0x08,// *
	0x08, 0x08, 0x3E, 0x08, 0x08,// +
	0x00, 0x50, 0x30, 0x00, 0x00,// ,
	0x08, 0x08, 0x08, 0x08, 0x08,// -
	0x00, 0x60, 0x60, 0x00, 0x00,// .
	0x20, 0x10, 0x08, 0x04, 0x02,// /
	0x3E, 0x51, 0x49, 0x45, 0x3E,// 0
	0x00, 0x42, 0x7F, 0x40, 0x00,// 1
	0x42, 0x61, 0x51, 0x49, 0x46,// 2
	0x21, 0x41, 0x45, 0x4B, 0x31,// 3
	0x18, 0x14, 0x12, 0x7F, 0x10,// 4
	0x27, 0x45, 0x45, 0x45, 0x39,// 5
	0x3C, 0x4A, 0x49, 0x49, 0x30,// 6
	0x01, 0x71, 0x09, 0x05, 0x03,// 7
	0x36, 0x49, 0x49, 0x49, 0x36,// 8
	0x06, 0x49, 0x49, 0x29, 0x1E,// 9
	0x00, 0x36, 0x36, 0x00, 0x00,// :
	0x00, 0x56, 0x36, 0x00, 0x00,// ;
	0x00, 0x08, 0x14, 0x22, 0x41,// <
	0x14, 0x14, 0x14, 0x14, 0x14,// =
	0x41, 0x22, 0x14, 0x08, 0x00,// >
	0x02, 0x01, 0x51, 0x09, 0x06,// ?
	0x32, 0x49, 0x79, 0x41, 0x3E,// @
	0x7E, 0x11, 0x11, 0x11, 0x7E,// A
	0x7F, 0x49, 0x49, 0x49, 0x36,// B
	0x3E, 0x41, 0x41, 0x41, 0x22,// C
	0x7F, 0x41, 0x41, 0x22, 0x1C,// D
	0x7F, 0x49, 0x49, 0x49, 0x41,// E
	0x7F, 0x09, 0x09, 0x01, 0x01,// F
	0x3E, 0x41, 0x41, 0x51, 0x32,// G
	0x7F, 0x08, 0x08, 0x08, 0x7F,// H
	0x00, 0x41, 0x7F, 0x41, 0x00,// I
	0x20, 0x40, 0x41, 0x3F, 0x01,// J
	0x7F, 0x08, 0x14, 0x22, 0x41,// K
	0x7F, 0x40, 0x40, 0x40, 0x40,// L
	0x7F, 0x02, 0x04, 0x02, 0x7F,// M
	0x7F, 0x04, 0x08, 0x10, 0x7F,// N
	0x3E, 0x41, 0x41, 0x41, 0x3E,// O
	0x7F, 0x09, 0x09, 0x09, 0x06,// P
	0x3E, 0x41, 0x51, 0x21, 0x5E,// Q
	0x7F, 0x09, 0x19, 0x29, 0x46,// R
	0x46, 0x49, 0x49, 0x49, 0x31,// S
	0x01, 0x01, 0x7F, 0x01, 0x01,// T
	0x3F, 0x40, 0x40, 0x40, 0x3F,// U
	0x1F, 0x20, 0x40, 0x20, 0x1F,// V
	0x7F, 0x20, 0x18, 0x20, 0x7F,// W
	0x63, 0x14, 0x08, 0x14, 0x63,// X
	0x03, 0x04, 0x78, 0x04, 0x03,// Y
	0x61, 0x51, 0x49, 0x45, 0x43,// Z
	0x00, 0x00, 0x7F, 0x41, 0x41,// [
	0x02, 0x04, 0x08, 0x10, 0x20,// "\"
	0x41, 0x41, 0x7F, 0x00, 0x00,// ]
	0x04, 0x02, 0x01, 0x02, 0x04,// ^
	0x40, 0x40, 0x40, 0x40, 0x40,// _
	0x00, 0x01, 0x02, 0x04, 0x00,// `
	0x20, 0x54, 0x54, 0x54, 0x78,// a
	0x7F, 0x48, 0x44, 0x44, 0x38,// b
	0x38, 0x44, 0x44, 0x44, 0x20,// c
	0x38, 0x44, 0x44, 0x48, 0x7F,// d
	0x38, 0x54, 0x54, 0x54, 0x18,// e
	0x08, 0x7E, 0x09, 0x01, 0x02,// f
	0x08, 0x14, 0x54, 0x54, 0x3C,// g
	0x7F, 0x08, 0x04, 0x04, 0x78,// h
	0x00, 0x44, 0x7D, 0x40, 0x00,// i
	0x20, 0x40, 0x44, 0x3D, 0x00,// j
	0x00, 0x7F, 0x10, 0x28, 0x44,// k
	0x00, 0x41, 0x7F, 0x40, 0x00,// l
	0x7C, 0x04, 0x18, 0x04, 0x78,// m
	0x7C, 0x08, 0x04, 0x04, 0x78,// n
	0x38, 0x44, 0x44, 0x44, 0x38,// o
	0x7C, 0x14, 0x14, 0x14, 0x08,// p
	0x08, 0x14, 0x14, 0x18, 0x7C,// q
	0x7C, 0x08, 0x04, 0x04, 0x08,// r
	0x48, 0x54, 0x54, 0x54, 0x20,// s
	0x04, 0x3F, 0x44, 0x40, 0x20,// t
	0x3C, 0x40, 0x40, 0x20, 0x7C,// u
	0x1C, 0x20, 0x40, 0x20, 0x1C,// v
	0x3C, 0x40, 0x30, 0x40, 0x3C,// w
	0x44, 0x28, 0x10, 0x28, 0x44,// x
	0x0C, 0x50, 0x50, 0x50, 0x3C,// y
	0x44, 0x64, 0x54, 0x4C, 0x44,// z
	0x00, 0x08, 0x36, 0x41, 0x00,// {
	0x00, 0x00, 0x7F, 0x00, 0x00,// |
	0x00, 0x41, 0x36, 0x08, 0x00,// }
	0x08, 0x08, 0x2A, 0x1C, 0x08,// ->
	0x08, 0x1C, 0x2A, 0x08, 0x08, // <-
    0x08, 0x1C, 0x3E, 0x1C, 0x08,// menu select \x80
    0x00, 0x06, 0x09, 0x09, 0x06,// temperature \x81
    0x10, 0x18, 0x1C, 0x18, 0x10,//  up arrow \x82
    0x08, 0x18, 0x38, 0x18, 0x08// down arrow \x83

};

void lcd_lib_draw_string(int16_t x, int16_t y, const char* str)
{
    uint8_t* dst;
    uint8_t* dst2;
    uint8_t yshift;
    uint8_t yshift2;
    const uint8_t* src;
    
    while (*str) {
        if (1) {
            if (x>=LCD_GFX_WIDTH) {
                return;
            }
            if (y>=LCD_GFX_HEIGHT) {
                return;
            }
            if (y<-7) {
                return;
            }
            if (x<-5) {
                x+=5;
                str++;
                continue;
            }
            if (y<0) {
                dst2 = lcd_buffer + x;
                yshift = 8;
                yshift2 = -y;
            }
            else if(y+7>LCD_GFX_HEIGHT){
                dst = lcd_buffer + x + 7 * LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8;
            }
            else{
                dst = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH;
                dst2 = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8 - yshift;
            }
            
            if (yshift != 8) {
                src = lcd_font_7x5 + ((uint8_t)*str - ' ') * 5;
                for (int i=0; i<5; i++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst = (*dst) | pgm_read_byte(src++) << yshift;
                    }
                    else{
                        src++;
                    }
                    dst++;
                    x++;
                }
                dst++;
                x-=5;
            }
            
            if (yshift2 != 8){
                src = lcd_font_7x5 + ((uint8_t)*str - ' ') * 5;
                for (int j=0; j<5; j++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst2 = (*dst2) | pgm_read_byte(src++) >> yshift2;
                    }
                    else{
                        src++;
                    }
                    dst2++;
                    x++;
                }
                dst2++;
                x-=5;
            }
            str++;
            x+=6;
        }
    }
}

void lcd_lib_clear_string(int16_t x, int16_t y, const char* str)
{
    uint8_t* dst;
    uint8_t* dst2;
    uint8_t yshift;
    uint8_t yshift2;
    const uint8_t* src;
    
    while (*str) {
        if (1) {
            if (x>=LCD_GFX_WIDTH) {
                return;
            }
            if (y>=LCD_GFX_HEIGHT) {
                return;
            }
            if (y<-7) {
                return;
            }
            if (x<-5) {
                x+=5;
                str++;
                continue;
            }
            if (y<0) {
                dst2 = lcd_buffer + x;
                yshift = 8;
                yshift2 = -y;
            }
            else if(y+7>LCD_GFX_HEIGHT){
                dst = lcd_buffer + x + 7 * LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8;
            }
            else{
                dst = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH;
                dst2 = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8 - yshift;
            }
            
            if (yshift != 8) {
                src = lcd_font_7x5 + ((uint8_t)*str - ' ') * 5;
                for (int i=0; i<5; i++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst = (*dst) &~(pgm_read_byte(src++) << yshift);
                    }
                    else{
                        src++;
                    }
                    dst++;
                    x++;
                }
                dst++;
                x-=5;
            }
            
            if (yshift2 != 8){
                src = lcd_font_7x5 + ((uint8_t)*str - ' ') * 5;
                for (int j=0; j<5; j++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst2 = (*dst2) &~(pgm_read_byte(src++) >> yshift2);
                    }
                    else{
                        src++;
                    }
                    dst2++;
                    x++;
                }
                dst2++;
                x-=5;
            }
            str++;
            x+=6;
        }
    }
}

void lcd_lib_draw_string_center(int16_t y, const char* str)
{
    lcd_lib_draw_string(64 - strlen(str) * 3, y, str);
}

void lcd_lib_clear_string_center(int16_t y, const char* str)
{
    lcd_lib_clear_string(64 - strlen(str) * 3, y, str);
}

void lcd_lib_draw_stringP(int16_t x, int16_t y, const char* pstr)
{
    uint8_t* dst;
    uint8_t* dst2;
    uint8_t yshift;
    uint8_t yshift2;
    const uint8_t* src;
    
    for(uint8_t c = pgm_read_byte(pstr); c; c = pgm_read_byte(++pstr)){
        if (1) {
            if (x>=LCD_GFX_WIDTH) {
                return;
            }
            if (y>=LCD_GFX_HEIGHT) {
                return;
            }
            if (y<-7) {
                return;
            }
            if (x<-5) {
                x+=5;
                continue;
            }
            if (y<0) {
                dst2 = lcd_buffer + x;
                yshift = 8;
                yshift2 = -y;
            }
            else if(y+7>LCD_GFX_HEIGHT){
                dst = lcd_buffer + x + 7 * LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8;
            }
            else{
                dst = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH;
                dst2 = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8 - yshift;
            }
            
            if (yshift != 8) {
                src = lcd_font_7x5 + ((uint8_t)c - ' ') * 5;
                for (int i=0; i<5; i++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst = (*dst) | pgm_read_byte(src++) << yshift;
                    }
                    else{
                        src++;
                    }
                    dst++;
                    x++;
                }
                dst++;
                x-=5;
            }
            
            if (yshift2 != 8){
                src = lcd_font_7x5 + ((uint8_t)c - ' ') * 5;
                for (int j=0; j<5; j++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst2 = (*dst2) | pgm_read_byte(src++) >> yshift2;
                    }
                    else{
                        src++;
                    }
                    dst2++;
                    x++;
                }
                dst2++;
                x-=5;
            }
            x+=6;
        }
    }
}

void lcd_lib_clear_stringP(int16_t x, int16_t y, const char* pstr)
{
    uint8_t* dst;
    uint8_t* dst2;
    uint8_t yshift;
    uint8_t yshift2;
    const uint8_t* src;
    
    for(uint8_t c = pgm_read_byte(pstr); c; c = pgm_read_byte(++pstr)){
        if (1) {
            if (x>=LCD_GFX_WIDTH) {
                return;
            }
            if (y>=LCD_GFX_HEIGHT) {
                return;
            }
            if (y<-7) {
                return;
            }
            if (x<-5) {
                x+=5;
                continue;
            }
            if (y<0) {
                dst2 = lcd_buffer + x;
                yshift = 8;
                yshift2 = -y;
            }
            else if(y+7>LCD_GFX_HEIGHT){
                dst = lcd_buffer + x + 7 * LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8;
            }
            else{
                dst = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH;
                dst2 = lcd_buffer + x + (y / 8) * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
                yshift = y % 8;
                yshift2 = 8 - yshift;
            }
            
            if (yshift != 8) {
                src = lcd_font_7x5 + ((uint8_t)c - ' ') * 5;
                for (int i=0; i<5; i++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst = (*dst) &~(pgm_read_byte(src++) << yshift);
                    }
                    else{
                        src++;
                    }
                    dst++;
                    x++;
                }
                dst++;
                x-=5;
            }
            
            if (yshift2 != 8){
                src = lcd_font_7x5 + ((uint8_t)c - ' ') * 5;
                for (int j=0; j<5; j++) {
                    if (x>=0 && x<LCD_GFX_WIDTH) {
                        *dst2 = (*dst2) &~(pgm_read_byte(src++) >> yshift2);
                    }
                    else{
                        src++;
                    }
                    dst2++;
                    x++;
                }
                dst2++;
                x-=5;
            }
            x+=6;
        }
    }
}

void lcd_lib_draw_string_centerP(int16_t y, const char* pstr)
{
    lcd_lib_draw_stringP(64 - strlen_P(pstr) * 3, y, pstr);
}

void lcd_lib_clear_string_centerP(int16_t y, const char* pstr)
{
    lcd_lib_clear_stringP(64 - strlen_P(pstr) * 3, y, pstr);
}

void lcd_lib_draw_string_center_atP(int16_t x, int16_t y, const char* pstr)
{
    const char* split = strchr_P(pstr, '|');
    if (split)
    {
        char buf[10];
        strncpy_P(buf, pstr, split - pstr);
        buf[split - pstr] = '\0';
        lcd_lib_draw_string(x - strlen(buf) * 3, y - 5, buf);
        lcd_lib_draw_stringP(x - strlen_P(split+1) * 3, y + 5, split+1);
    }else{
        lcd_lib_draw_stringP(x - strlen_P(pstr) * 3, y, pstr);
    }
}

void lcd_lib_draw_string_center_at(int16_t x, int16_t y, const char* pstr)
{
    lcd_lib_draw_string(x - strlen(pstr) * 3, y, pstr);
}

void lcd_lib_clear_string_center_atP(int16_t x, int16_t y, const char* pstr)
{
    const char* split = strchr_P(pstr, '|');
    if (split)
    {
        char buf[10];
        strncpy_P(buf, pstr, split - pstr);
        buf[split - pstr] = '\0';
        lcd_lib_clear_string(x - strlen(buf) * 3, y - 5, buf);
        lcd_lib_clear_stringP(x - strlen_P(split+1) * 3, y + 5, split+1);
    }else{
        lcd_lib_clear_stringP(x - strlen_P(pstr) * 3, y, pstr);
    }
}



void lcd_lib_move_point(uint8_t xDst, uint8_t yDst, uint8_t xSrc, uint8_t ySrc)
{
    uint8_t ySrcOffset=ySrc%8;
    int8_t maskErr=yDst%8-ySrcOffset;
    int8_t maskSrc=0x01<<ySrcOffset;

    
    uint8_t* dst = lcd_buffer + (yDst / 8) * LCD_GFX_WIDTH + xDst;
    uint8_t* src = lcd_buffer + (ySrc / 8) * LCD_GFX_WIDTH + xSrc;
    
    if (maskErr<0) {
        maskErr=-maskErr;
        *dst |= (*src & (maskSrc))>>(maskErr);
    }
    else{
        *dst |= (*src & (maskSrc))<<(maskErr);
    }
    *src &= ~maskSrc;
}

void lcd_lib_remove_point(uint8_t xSrc, uint8_t ySrc)
{
    int8_t maskSrc=0x01<<(ySrc%8);

    uint8_t* src = lcd_buffer + (ySrc / 8) * LCD_GFX_WIDTH + xSrc;
    
    *src &= ~maskSrc;
}



void lcd_lib_copy_hline(int16_t yDst, int16_t ySrc)
{
    
    if (yDst>=LCD_GFX_HEIGHT) {
        yDst=LCD_GFX_HEIGHT-1;
    }
    
    if (yDst<0) {
        yDst=0;
    }

    
    uint8_t* src = lcd_buffer + (ySrc / 8) * LCD_GFX_WIDTH;
    uint8_t maskSrc = 0x01 << (ySrc % 8);
    uint8_t* dst = lcd_buffer + (yDst / 8) * LCD_GFX_WIDTH;
    int8_t maskErr = (yDst % 8) - (ySrc % 8);
    
    if (maskErr>=0) {
        for (int i=0; i<128; i++) {
            *dst |= (*src & maskSrc)<<(maskErr);
            *src &= ~maskSrc;
            src++;
            dst++;
        }
    }
    else{
        maskErr=-maskErr;
        for (int j=0; j<128; j++) {
            *dst |= (*src & maskSrc)>>(maskErr);
            *src &= ~maskSrc;
            src++;
            dst++;
        }
    }
}


void lcd_lib_draw_hline(int16_t x0, int16_t x1, int16_t y)
{
    if (y>=LCD_GFX_HEIGHT || y<0 || x0>=LCD_GFX_WIDTH ||x1<0) {
        return;
    }
    if (x0<0) {
        x0=0;
    }
    if (x1>=LCD_GFX_WIDTH) {
        x1=LCD_GFX_WIDTH-1;
    }

    uint8_t* dst = lcd_buffer + x0 + (y / 8) * LCD_GFX_WIDTH;
    uint8_t mask = 0x01 << (y % 8);
    
    while(x0 <= x1)
    {
        *dst++ |= mask;
        x0 ++;
    }
}

void lcd_lib_draw_vline(int16_t x, int16_t y0, int16_t y1)
{
    if (x>=LCD_GFX_WIDTH || x<0 || y0>=LCD_GFX_HEIGHT ||y1<0) {
        return;
    }
    if (y0<0) {
        y0=0;
    }
    if (y1>=LCD_GFX_HEIGHT) {
        y1=LCD_GFX_HEIGHT-1;
    }
    
    uint8_t* dst0 = lcd_buffer + x + (y0 / 8) * LCD_GFX_WIDTH;
    uint8_t* dst1 = lcd_buffer + x + (y1 / 8) * LCD_GFX_WIDTH;
    if (dst0 == dst1)
    {
        *dst0 |= (0xFF << (y0 % 8)) & (0xFF >> (7 - (y1 % 8)));
    }else{
        *dst0 |= 0xFF << (y0 % 8);
        dst0 += LCD_GFX_WIDTH;
        while(dst0 != dst1)
        {
            *dst0 = 0xFF;
            dst0 += LCD_GFX_WIDTH;
        }
        *dst1 |= 0xFF >> (7 - (y1 % 8));
    }
}

void lcd_lib_draw_box(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    lcd_lib_draw_vline(x0, y0+1, y1-1);
    lcd_lib_draw_vline(x1, y0+1, y1-1);
    lcd_lib_draw_hline(x0+1, x1-1, y0);
    lcd_lib_draw_hline(x0+1, x1-1, y1);
}

void lcd_lib_draw_shade(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    uint8_t* dst0 = lcd_buffer + x0 + (y0 / 8) * LCD_GFX_WIDTH;
    uint8_t* dst1 = lcd_buffer + x0 + (y1 / 8) * LCD_GFX_WIDTH;
    if (dst0 == dst1)
    {
        //uint8_t mask = (0xFF << (y0 % 8)) & (0xFF >> (7 - (y1 % 8)));
        //*dstA0 |= (mask & 0xEE);
    }else{
        uint8_t mask = 0xFF << (y0 % 8);
        uint8_t* dst = dst0;
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ |= mask & ((x & 1) ? 0xAA : 0x55);
        dst0 += LCD_GFX_WIDTH;
        while(dst0 != dst1)
        {
            dst = dst0;
            for(uint8_t x=x0; x<=x1; x++)
                *dst++ |= (x & 1) ? 0xAA : 0x55;
            dst0 += LCD_GFX_WIDTH;
        }
        dst = dst1;
        mask = 0xFF >> (7 - (y1 % 8));
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ |= mask & ((x & 1) ? 0xAA : 0x55);
    }
}

void lcd_lib_clear()
{
    memset(lcd_buffer, 0, sizeof(lcd_buffer));
}

void lcd_lib_set()
{
    memset(lcd_buffer, 0xFF, sizeof(lcd_buffer));
}

void lcd_lib_clear(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (x1<0 || y1<0 || x0>=LCD_GFX_WIDTH || y0>=LCD_GFX_HEIGHT) {
        return;
    }
    if (x0<0) {
        x0=0;
    }
    if (y0<0) {
        y0=0;
    }
    if (x1>=LCD_GFX_WIDTH) {
        x1=LCD_GFX_WIDTH-1;
    }
    if (y1>=LCD_GFX_HEIGHT) {
        y1=LCD_GFX_HEIGHT-1;
    }
    
    uint8_t* dst0 = lcd_buffer + x0 + (y0 / 8) * LCD_GFX_WIDTH;
    uint8_t* dst1 = lcd_buffer + x0 + (y1 / 8) * LCD_GFX_WIDTH;
    if (dst0 == dst1)
    {
        uint8_t mask = (0xFF << (y0 % 8)) & (0xFF >> (7 - (y1 % 8)));
        for(uint8_t x=x0; x<=x1; x++)
            *dst0++ &=~mask;
    }else{
        uint8_t mask = 0xFF << (y0 % 8);
        uint8_t* dst = dst0;
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ &=~mask;
        dst0 += LCD_GFX_WIDTH;
        while(dst0 != dst1)
        {
            dst = dst0;
            for(uint8_t x=x0; x<=x1; x++)
                *dst++ = 0x00;
            dst0 += LCD_GFX_WIDTH;
        }
        dst = dst1;
        mask = 0xFF >> (7 - (y1 % 8));
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ &=~mask;
    }
}

void lcd_lib_invert(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (x1<0 || y1<0 || x0>=LCD_GFX_WIDTH || y0>=LCD_GFX_HEIGHT) {
        return;
    }
    if (x0<0) {
        x0=0;
    }
    if (y0<0) {
        y0=0;
    }
    if (x1>=LCD_GFX_WIDTH) {
        x1=LCD_GFX_WIDTH-1;
    }
    if (y1>=LCD_GFX_HEIGHT) {
        y1=LCD_GFX_HEIGHT-1;
    }
    uint8_t* dst0 = lcd_buffer + x0 + (y0 / 8) * LCD_GFX_WIDTH;
    uint8_t* dst1 = lcd_buffer + x0 + (y1 / 8) * LCD_GFX_WIDTH;
    if (dst0 == dst1)
    {
        uint8_t mask = (0xFF << (y0 % 8)) & (0xFF >> (7 - (y1 % 8)));
        for(uint8_t x=x0; x<=x1; x++)
            *dst0++ ^= mask;
    }else{
        uint8_t mask = 0xFF << (y0 % 8);
        uint8_t* dst = dst0;
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ ^= mask;
        dst0 += LCD_GFX_WIDTH;
        while(dst0 != dst1)
        {
            dst = dst0;
            for(uint8_t x=x0; x<=x1; x++)
                *dst++ ^= 0xFF;
            dst0 += LCD_GFX_WIDTH;
        }
        dst = dst1;
        mask = 0xFF >> (7 - (y1 % 8));
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ ^= mask;
    }
}

void lcd_lib_set(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (x1<0 || y1<0 || x0>=LCD_GFX_WIDTH || y0>=LCD_GFX_HEIGHT) {
        return;
    }
    if (x0<0) {
        x0=0;
    }
    if (y0<0) {
        y0=0;
    }
    if (x1>=LCD_GFX_WIDTH) {
        x1=LCD_GFX_WIDTH-1;
    }
    if (y1>=LCD_GFX_HEIGHT) {
        y1=LCD_GFX_HEIGHT-1;
    }
    uint8_t* dst0 = lcd_buffer + x0 + (y0 / 8) * LCD_GFX_WIDTH;
    uint8_t* dst1 = lcd_buffer + x0 + (y1 / 8) * LCD_GFX_WIDTH;
    if (dst0 == dst1)
    {
        uint8_t mask = (0xFF << (y0 % 8)) & (0xFF >> (7 - (y1 % 8)));
        for(uint8_t x=x0; x<=x1; x++)
            *dst0++ |= mask;
    }else{
        uint8_t mask = 0xFF << (y0 % 8);
        uint8_t* dst = dst0;
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ |= mask;
        dst0 += LCD_GFX_WIDTH;
        while(dst0 != dst1)
        {
            dst = dst0;
            for(uint8_t x=x0; x<=x1; x++)
                *dst++ = 0xFF;
            dst0 += LCD_GFX_WIDTH;
        }
        dst = dst1;
        mask = 0xFF >> (7 - (y1 % 8));
        for(uint8_t x=x0; x<=x1; x++)
            *dst++ |= mask;
    }
}

void lcd_lib_draw_gfx(int16_t x, int16_t y, const uint8_t* gfx)
{
    uint8_t w = pgm_read_byte(gfx++);
    uint8_t h = (pgm_read_byte(gfx++) + 7) / 8;
    uint8_t shift = y % 8;
    uint8_t shift2 = 8 - shift;
    y /= 8;
    
    for(; h; h--)
    {
        if (y >= LCD_GFX_HEIGHT / 8) break;
        
        uint8_t* dst0 = lcd_buffer + x + y * LCD_GFX_WIDTH;
        uint8_t* dst1 = lcd_buffer + x + y * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
        for(uint8_t _w = w; _w; _w--)
        {
            uint8_t c = pgm_read_byte(gfx++);
            *dst0++ |= c << shift;
            if (shift && y < 7)
                *dst1++ |= c >> shift2;
        }
        y++;
    }
}

void lcd_lib_clear_gfx(int16_t x, int16_t y, const uint8_t* gfx)
{
    uint8_t w = pgm_read_byte(gfx++);
    uint8_t h = (pgm_read_byte(gfx++) + 7) / 8;
    uint8_t shift = y % 8;
    uint8_t shift2 = 8 - shift;
    y /= 8;
    
    for(; h; h--)
    {
        if (y >= LCD_GFX_HEIGHT / 8) break;
        
        uint8_t* dst0 = lcd_buffer + x + y * LCD_GFX_WIDTH;
        uint8_t* dst1 = lcd_buffer + x + y * LCD_GFX_WIDTH + LCD_GFX_WIDTH;
        for(uint8_t _w = w; _w; _w--)
        {
            uint8_t c = pgm_read_byte(gfx++);
            *dst0++ &=~(c << shift);
            if (shift && y < 7)
                *dst1++ &=~(c >> shift2);
        }
        y++;
    }
}

#define LCDDetailY  52


void lcd_lib_move_horizontal(int16_t x)
{
    uint8_t* dst0;  //source
    uint8_t* dst1;  //destination
    uint8_t y;
    uint8_t mask = 0xff>>(7-(LCDDetailY%8));
    int16_t i;
    
    if (x==0) {
        return;
    }
    
    if (x<=-LCD_GFX_WIDTH || x>=LCD_GFX_WIDTH) {
        lcd_lib_clear(0, 0, LCD_GFX_WIDTH, LCDDetailY);
        return;
    }

    
        if (x>0) {
            
            for (y=0; y<((LCDDetailY+1)/8); y++) {
//              dst0 = lcd_buffer + LCD_GFX_WIDTH - x + y * LCD_GFX_WIDTH;
                dst1 = lcd_buffer + LCD_GFX_WIDTH -1 + y * LCD_GFX_WIDTH;
                dst0 = dst1-x;
                
                for (i=x; i < LCD_GFX_WIDTH; i++) {
                    *dst1=*dst0;
                    dst0--;
                    dst1--;
                }
            }
            
            if (mask != 0xff) {
                dst1 = lcd_buffer + LCD_GFX_WIDTH-1 + y * LCD_GFX_WIDTH;
                dst0 = dst1-x;
                
                for (int16_t i=x; i < LCD_GFX_WIDTH; i++) {
                    *dst1 &= ~mask;
                    *dst1 |= (*dst0) & mask;
                    dst0--;
                    dst1--;
                }
            }
        }
        else{
            for (y=0; y<((LCDDetailY+1)/8); y++) {

    //            dst0 = lcd_buffer - x + y * LCD_GFX_WIDTH;
                dst1 = lcd_buffer + y * LCD_GFX_WIDTH;
                dst0 = dst1-x;
                
                for (i=x; i > -LCD_GFX_WIDTH; i--) {
                    *dst1=*dst0;
                    dst0++;
                    dst1++;
                }
            }
            
            if (mask != 0xff) {
                dst1 = lcd_buffer + y * LCD_GFX_WIDTH;
                dst0 = dst1-x;
                
                for (i=x; i > -LCD_GFX_WIDTH; i--) {
                    *dst1 &= ~mask;
                    *dst1 |= (*dst0) & mask;
                    dst0++;
                    dst1++;
                }
            }
        }
    if (x>0) {
        lcd_lib_clear(0, 0, x, LCDDetailY);
    }
    else{
        lcd_lib_clear(LCD_GFX_WIDTH+x, 0, LCD_GFX_WIDTH-1, LCDDetailY);
    }
}

void lcd_lib_move_vertical(int16_t y)
{
    uint8_t* dst0;  //source
    uint8_t* dst1;  //destination
    uint8_t* dst2;  //source
    uint8_t mask = 0xff>>(7-(LCDDetailY%8));
    uint8_t sourceBuf;

    int i;
    
    if (y==0) {
        return;
    }
    
    if (y<-LCDDetailY || y>LCDDetailY) {
        lcd_lib_clear(0, 0, LCD_GFX_WIDTH, LCDDetailY);
        return;
    }

    uint16_t yMoveSteps;
    uint8_t yShortOffset;
    
    
    if (y>0) {

        
        yShortOffset=y%8;
        
        if (mask != 0xff) {
            
            dst1=lcd_buffer+(LCD_GFX_WIDTH * ((LCDDetailY+1) / 8 + 1) - 1);       //The raw of the split by LCDDetailY
            dst0=dst1-(y/8)*LCD_GFX_WIDTH;
            dst2=dst0-LCD_GFX_WIDTH;
            
            for (i=0; i<LCD_GFX_WIDTH; i++) {

                sourceBuf =((*dst0)<<yShortOffset)|((*dst2)>>(8-yShortOffset));
                
                *dst1 &= ~mask;
                *dst1 |= (sourceBuf) & mask;
                dst0--;
                dst1--;
                dst2--;
            }

        }
        
        
        dst1=lcd_buffer+(LCD_GFX_WIDTH * ((LCDDetailY+1) / 8) - 1);
        dst0=dst1-(y/8)*LCD_GFX_WIDTH;
        dst2=dst0-LCD_GFX_WIDTH;
        
        yMoveSteps=((LCDDetailY+1)/8-y/8)*LCD_GFX_WIDTH;
        for (i=0; i<yMoveSteps; i++) {
            *dst1=((*dst0)<<yShortOffset)|((*dst2)>>(8-yShortOffset));
            dst0--;
            dst1--;
            dst2--;
        }
//        SERIAL_ECHOLN(freeMemory());
        lcd_lib_clear(0, 0, LCD_GFX_WIDTH-1, y-1);
    }
    else{
        y=-y;
        yShortOffset=y%8;
        
        dst1=lcd_buffer;
        dst0=lcd_buffer+(y/8)*LCD_GFX_WIDTH;
        dst2=dst0+LCD_GFX_WIDTH;
        
        yMoveSteps=((LCDDetailY+1)/8-y/8)*LCD_GFX_WIDTH;
        for (i=0; i<yMoveSteps; i++) {
            *dst1= ((*dst0)>>yShortOffset) | ((*dst2)<<(8-yShortOffset));
            dst0++;
            dst1++;
            dst2++;
        }
        
        if (mask != 0xff) {
            if (y/8==0) {
                dst1=lcd_buffer+LCD_GFX_WIDTH * ((LCDDetailY+1) / 8);       //The raw of the split by LCDDetailY
                dst0=dst1+y/8*LCD_GFX_WIDTH;
                dst2=dst0+LCD_GFX_WIDTH;
                
                for (i=0; i<LCD_GFX_WIDTH; i++) {

                    sourceBuf =((*dst0)>>yShortOffset)|((*dst2)<<(8-yShortOffset));
                    
                    *dst1 &= ~mask;
                    *dst1 |= (sourceBuf) & mask;
                    dst0++;
                    dst1++;
                    dst2++;
                }
            }
        }
        
        
        lcd_lib_clear(0, (LCDDetailY+1)-y, LCD_GFX_WIDTH-1, LCDDetailY);
    }
}

void lcd_lib_beep()
{
//#define _BEEP(c, n) for(int8_t _i=0;_i<c;_i++) { WRITE(BEEPER, HIGH); _delay_us(n); WRITE(BEEPER, LOW); _delay_us(n); }
//    _BEEP(20, 366);
//    _BEEP(10, 150);
//#undef _BEEP
    
    beepType=beepTypePress;
    
}

#ifdef PushButton
int16_t lcd_lib_encoder_pos_interrupt = 0;
#else
int8_t lcd_lib_encoder_pos_interrupt = 0;
#endif
int16_t lcd_lib_encoder_pos = 0;
bool lcd_lib_button_pressed = false;
bool lcd_lib_button_down;
//bool lcd_lib_button_any_pressed = false;



#ifdef PushButton
bool lcd_lib_button_up_down_reversed=false;
#endif
#define ENCODER_ROTARY_BIT_0 _BV(0)
#define ENCODER_ROTARY_BIT_1 _BV(1)
/* Warning: This function is called from interrupt context */
void lcd_lib_buttons_update_interrupt()
{
#ifdef PushButton
//  static uint8_t lastEncBits = 0;

  
  static uint16_t pushButtonUpTimer=0;
  static uint16_t pushButtonDownTimer=0;
  
  if (!READ(PushButtonUp)) {
//    lastEncBits|=ENCODER_ROTARY_BIT_0;
    pushButtonUpTimer++;
    
    if (pushButtonUpTimer == 2) {
      lcd_lib_encoder_pos_interrupt--;
    }
    
    if (pushButtonUpTimer >= 500>>5) {
      if ((pushButtonUpTimer & (0x007f>>5))==0x0000) {
        lcd_lib_encoder_pos_interrupt--;
      }
      if (pushButtonUpTimer >= 3000>>5) {
        if ((pushButtonUpTimer & (0x003f>>5))==0x0000) {
          lcd_lib_encoder_pos_interrupt--;
        }
        if (pushButtonUpTimer >= 6000>>5) {
//          if ((pushButtonUpTimer & (0x0003>>5))==0x0000) {
            lcd_lib_encoder_pos_interrupt--;
//          }
        }
      }
    }
  }
  else{
    pushButtonUpTimer=0;
  }
  
  
  if (!READ(PushButtonDown)) {
//    lastEncBits|=ENCODER_ROTARY_BIT_1;
    pushButtonDownTimer++;
    
    if (pushButtonDownTimer == 2) {
      lcd_lib_encoder_pos_interrupt++;
    }
    
    if (pushButtonDownTimer >= 500>>5) {
      if ((pushButtonDownTimer & (0x007f>>5))==0x0000) {
        lcd_lib_encoder_pos_interrupt++;
      }
      
      if (pushButtonDownTimer >= 3000>>5) {
        if ((pushButtonDownTimer & (0x003f>>5))==0x0000) {
          lcd_lib_encoder_pos_interrupt++;
        }
        if (pushButtonDownTimer >= 6000>>5) {
//          if ((pushButtonDownTimer & (0x0003>>5))==0x0000) {
            lcd_lib_encoder_pos_interrupt++;
//          }
        }
      }
    }
  }
  else{
    pushButtonDownTimer=0;
  }
  

  
  
  
#else
    static uint8_t lastEncBits = 0;
    
    uint8_t encBits = 0;
    if(!READ(BTN_EN1)) encBits |= ENCODER_ROTARY_BIT_0;
    if(!READ(BTN_EN2)) encBits |= ENCODER_ROTARY_BIT_1;
    
    if(encBits != lastEncBits)
    {
        switch(encBits)
        {
        case encrot0:
            if(lastEncBits==encrot3)
                lcd_lib_encoder_pos_interrupt++;
            else if(lastEncBits==encrot1)
                lcd_lib_encoder_pos_interrupt--;
            break;
        case encrot1:
            if(lastEncBits==encrot0)
                lcd_lib_encoder_pos_interrupt++;
            else if(lastEncBits==encrot2)
                lcd_lib_encoder_pos_interrupt--;
            break;
        case encrot2:
            if(lastEncBits==encrot1)
                lcd_lib_encoder_pos_interrupt++;
            else if(lastEncBits==encrot3)
                lcd_lib_encoder_pos_interrupt--;
            break;
        case encrot3:
            if(lastEncBits==encrot2)
                lcd_lib_encoder_pos_interrupt++;
            else if(lastEncBits==encrot0)
                lcd_lib_encoder_pos_interrupt--;
            break;
        }
        lastEncBits = encBits;
    }
  
#endif
}

void lcd_lib_buttons_update()
{

#ifdef PushButton
  uint8_t buttonState = !READ(PushButtonEnter);

#else
    uint8_t buttonState = !READ(BTN_ENC);
#endif
    lcd_lib_button_pressed = (buttonState && !lcd_lib_button_down);
    lcd_lib_button_down = buttonState;
    
    if ((lcd_lib_encoder_pos_interrupt || lcd_lib_button_pressed) && beepType!=beepTypeNormal) {
//        lcd_lib_button_any_pressed=true;
      beepType=beepTypePress;

    }
//    else{
////        lcd_lib_button_any_pressed=false;
//    }
  
#ifdef PushButton
    if (lcd_lib_button_up_down_reversed) {
        lcd_lib_encoder_pos -= lcd_lib_encoder_pos_interrupt;
    }
    else{
        lcd_lib_encoder_pos += lcd_lib_encoder_pos_interrupt;
    }
#else
    lcd_lib_encoder_pos += lcd_lib_encoder_pos_interrupt;
#endif
    
    lcd_lib_encoder_pos_interrupt = 0;

}

char* int_to_string(int i, char* temp_buffer, const char* p_postfix)
{
    char* c = temp_buffer;
    if (i < 0)
    {
        *c++ = '-'; 
        i = -i;
    }
    if (i >= 10000)
        *c++ = ((i/10000)%10)+'0';
    if (i >= 1000)
        *c++ = ((i/1000)%10)+'0';
    if (i >= 100)
        *c++ = ((i/100)%10)+'0';
    if (i >= 10)
        *c++ = ((i/10)%10)+'0';
    *c++ = ((i)%10)+'0';
    *c = '\0';
    if (p_postfix)
    {
        strcpy_P(c, p_postfix);
        c += strlen_P(p_postfix);
    }
    return c;
}

char* int_to_time_string(unsigned long i, char* temp_buffer)
{
    char* c = temp_buffer;
    uint8_t hours = (i / 60 / 60) % 60;
    uint8_t mins = (i / 60) % 60;
    uint8_t secs = i % 60;
  
  
    if (hours > 99)
    *c++ = '0' + hours / 100;
    *c++ = '0' + (hours / 10) % 10;
    *c++ = '0' + hours % 10;
    *c++ = ':';
    *c++ = '0' + mins / 10;
    *c++ = '0' + mins % 10;
    *c++ = ':';
    *c++ = '0' + secs / 10;
    *c++ = '0' + secs % 10;
    *c = '\0';
    return c;

}

char* float_to_string(float f, char* temp_buffer, const char* p_postfix)
{
    int32_t i = f * 100.0 + 0.5;
    char* c = temp_buffer;
    if (i < 0)
    {
        *c++ = '-'; 
        i = -i;
    }
    if (i >= 10000)
        *c++ = ((i/10000)%10)+'0';
    if (i >= 1000)
        *c++ = ((i/1000)%10)+'0';
    *c++ = ((i/100)%10)+'0';
    *c++ = '.';
    if (i >= 10)
        *c++ = ((i/10)%10)+'0';
    *c++ = ((i)%10)+'0';
    *c = '\0';
    if (p_postfix)
    {
        strcpy_P(c, p_postfix);
        c += strlen_P(p_postfix);
    }
    return c;
}

#endif//ENABLE_ULTILCD2
