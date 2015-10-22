#include "configuration.h"
#ifdef ENABLE_ULTILCD2
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "UltiLCD2_gfx.h"
#include "UltiLCD2_menu_material.h"
#include "UltiLCD2_menu_print.h"
#include "UltiLCD2_menu_first_run.h"
#include "UltiLCD2_menu_maintenance.h"
#include "cardreader.h"
#include "ConfigurationStore.h"
#include "temperature.h"
#include "pins.h"
#include "UltiLCD2_low_lib.h"

#ifdef SDUPS
#include "SDUPS.h"
#endif

#define SERIAL_CONTROL_TIMEOUT 0

unsigned long lastSerialCommandTime;
bool serialScreenShown;
uint8_t led_brightness_level = 100;
uint8_t led_mode = LED_MODE_ALWAYS_ON;

//#define SPECIAL_STARTUP

static void lcd_menu_startup();
#ifdef SPECIAL_STARTUP
static void lcd_menu_special_startup();
#endif//SPECIAL_STARTUP

void lcd_init()
{
    lcd_lib_init();
    if (!lcd_material_verify_material_settings())
    {
        lcd_material_reset_defaults();
        for(uint8_t e=0; e<EXTRUDERS; e++)
            lcd_material_set_material(0, e);
    }
    lcd_material_read_current_material();
    currentMenu = lcd_menu_startup;
    analogWrite(LED_PIN, 0);
    lastSerialCommandTime = millis() - SERIAL_CONTROL_TIMEOUT;
}

void lcd_update()
{
    if (!lcd_lib_update_ready()) return;
    lcd_lib_buttons_update();
    card.updateSDInserted();
    
    if (led_glow_dir)
    {
        led_glow-=2;
        if (led_glow == 0) led_glow_dir = 0;
    }else{
        led_glow+=2;
        if (led_glow == 126) led_glow_dir = 1;
    }

    if (IsStopped())
    {
        lcd_lib_clear();
        lcd_lib_draw_stringP(15, 10, PSTR("ERROR - STOPPED"));
        switch(StoppedReason())
        {
        case STOP_REASON_MAXTEMP:
        case STOP_REASON_MINTEMP:
            lcd_lib_draw_stringP(15, 20, PSTR("Temp sensor"));
            break;
        case STOP_REASON_MAXTEMP_BED:
            lcd_lib_draw_stringP(15, 20, PSTR("Temp sensor BED"));
            break;
        case STOP_REASON_HEATER_ERROR:
            lcd_lib_draw_stringP(15, 20, PSTR("Heater error"));
            break;
        case STOP_REASON_SAFETY_TRIGGER:
            lcd_lib_draw_stringP(15, 20, PSTR("Safety circuit"));
            break;
        case STOP_REASON_Z_ENDSTOP_BROKEN_ERROR:
            lcd_lib_draw_stringP(15, 20, PSTR("Z switch broken"));
            break;
        case STOP_REASON_Z_ENDSTOP_STUCK_ERROR:
            lcd_lib_draw_stringP(15, 20, PSTR("Z switch stuck"));
            break;
        case STOP_REASON_XY_ENDSTOP_BROKEN_ERROR:
            lcd_lib_draw_stringP(15, 20, PSTR("X or Y switch broken"));
            break;
        case STOP_REASON_XY_ENDSTOP_STUCK_ERROR:
            lcd_lib_draw_stringP(15, 20, PSTR("X or Y switch stuck"));
            break;
         case STOP_REASON_REDUNDANT_TEMP:
            lcd_lib_draw_stringP(15, 20, PSTR("Redundant Temp"));
            break;
        }
        lcd_lib_draw_stringP(1, 40, PSTR("Contact:"));
    
    lcd_lib_draw_stringP(1, 50, PSTR("support@dreammaker.cc"));
        LED_ERROR();
        lcd_lib_update_screen();
    }else if (millis() - lastSerialCommandTime < SERIAL_CONTROL_TIMEOUT)
    {
        if (!serialScreenShown)
        {
            lcd_lib_clear();
            lcd_lib_draw_string_centerP(0, PSTR("Printing with USB..."));
          
          char buffer[32];
          sprintf_P(buffer,PSTR("isBLEUpdate:%i"), int(isBLEUpdate));
          lcd_lib_draw_string_center(10, buffer);

//          lcd_lib_draw_string_center(20, cmdbuffer[0]);
//          lcd_lib_draw_string_center(30, cmdbuffer[1]);
//          lcd_lib_draw_string_center(40, cmdbuffer[2]);
//          lcd_lib_draw_string_center(50, cmdbuffer[3]);

            serialScreenShown = true;
        }
        if (printing_state == PRINT_STATE_HEATING || printing_state == PRINT_STATE_HEATING_BED || printing_state == PRINT_STATE_HOMING)
            lastSerialCommandTime = millis();
        lcd_lib_update_screen();
    }else{
        serialScreenShown = false;
        currentMenu();
        lcd_lib_update_screen();
        if (postMenuCheck) postMenuCheck();
    }
}


#define RefreshFrequency 20
#define DropFrequency 50
#define DropAcc 32
#define DropVel 32
void lcd_menu_startup()
{
    LED_NORMAL();
    
    static boolean isFirstTime=true;
    static unsigned long startTime;
    unsigned long allTime;
    
    if (isFirstTime) {
        isFirstTime=false;
        startTime=millis();
    }
    
    allTime=millis()-startTime;
    
    lcd_lib_clear();
    lcd_lib_draw_gfx(0, 22, overlordTextGfx);
    
    int endLine=64;
    int i,j;
    
    for (j=0; j<endLine; j++) {
        int lineStartTime = ((63-j)<<6) + 256 - allTime;
        
        if (lineStartTime + 127*20 < 0) {
            if (j==0) {
                
                isFirstTime=true;
                
                if (led_mode == LED_MODE_ALWAYS_ON)
                    analogWrite(LED_PIN, 255 * led_brightness_level / 100);
                led_glow = led_glow_dir = 0;
                LED_NORMAL();
                lcd_lib_beep();
                
                if (!IS_FIRST_RUN_DONE())
                  {
                    currentMenu = lcd_menu_first_run_init;
                  }else{
                      currentMenu = lcd_menu_main;
                  }
            }
            continue;
        }
        
        for (i=0; i<128; i++) {
            
            if (lineStartTime>=0) {
                
                //            int s= DropVel*lineStartTime+DropAcc*lineStartTime*lineStartTime;
                long s= j - (((long)lineStartTime*(long)lineStartTime)>>12);   //+lineStartTime*lineStartTime>14;
                
                if (s>=0) {
                    lcd_lib_move_point(i, s, i, j);
                }
                else{
                    lcd_lib_clear(i, j, 127, j);
                    break;
                }
            }
            lineStartTime+=20;
        }
    }
    
    if (led_mode == LED_MODE_ALWAYS_ON)
        analogWrite(LED_PIN, int(led_glow << 1) * led_brightness_level / 100);
    if (lcd_lib_button_pressed)
      {
        isFirstTime=true;
        if (led_mode == LED_MODE_ALWAYS_ON)
            analogWrite(LED_PIN, 255 * led_brightness_level / 100);
        led_glow = led_glow_dir = 0;
        LED_NORMAL();
        lcd_lib_beep();
        
#ifdef SPECIAL_STARTUP
        currentMenu = lcd_menu_special_startup;
#else
        if (!IS_FIRST_RUN_DONE()){
            currentMenu = lcd_menu_first_run_init;
        }
        else{
            currentMenu = lcd_menu_main;
        }
#endif//SPECIAL_STARTUP
      }
}


#ifdef SPECIAL_STARTUP
static void lcd_menu_special_startup()
{
    LED_GLOW();

    lcd_lib_clear();
    lcd_lib_draw_gfx(7, 12, specialStartupGfx);
    lcd_lib_draw_stringP(3, 2, PSTR("Welcome"));
    lcd_lib_draw_string_centerP(47, PSTR("To the Ultimaker2"));
    lcd_lib_draw_string_centerP(55, PSTR("experience!"));
    if (lcd_lib_button_pressed)
    {
        if (!IS_FIRST_RUN_DONE())
        {
            lcd_change_to_menu(lcd_menu_first_run_init);
        }else{
            lcd_change_to_menu(lcd_menu_main);
        }
    }
}
#endif//SPECIAL_STARTUP

void doCooldown()
{
    for(uint8_t n=0; n<EXTRUDERS; n++)
        setTargetHotend(0, n);
    setTargetBed(0);
    fanSpeed = lround(255 * int(fanSpeedPercent) / 100.0) ;
}

static char* lcd_menu_main_item(uint8_t nr)
{
    if (nr == 0)
        strcpy_P(card.longFilename, PSTR("\x83""Print"));
    else
        strcpy_P(card.longFilename, PSTR("???"));
    return card.longFilename;
}

static void lcd_menu_main_details(uint8_t nr)
{
    char buffer[22];
    if (nr == 0)
        lcd_draw_detailP(PSTR("Print from SD card."));
}

void lcd_menu_main()
{
    LED_NORMAL();
    if (lcd_lib_encoder_pos < 0) lcd_lib_encoder_pos = 0;
    
    lcd_normal_menu(PSTR("Main Menu"), 1, lcd_menu_main_item, lcd_menu_main_details);
    lcd_lib_draw_gfx(0, 0, printGfx);
    
    if (IS_SELECTED_SCROLL(1))
    {
        lcd_change_to_menu(lcd_menu_maintenance, SCROLL_MENU_ITEM_POS(0), MenuDown);
    }
    
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_SCROLL(0))
        {
            if (SDUPSIsWorking()) {
                lcd_change_to_menu(lcd_menu_print_resume_option,SCROLL_MENU_ITEM_POS(0), MenuForward);
            }
            else{
                lcd_clear_cache();
                lcd_change_to_menu(lcd_menu_print_select, SCROLL_MENU_ITEM_POS(0), MenuForward);
            }
        }
    }
}

void lcd_menu_power_check()
{
    LED_GLOW_POWERERROR();
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(20, PSTR("Connect the Power"));
    lcd_lib_draw_string_centerP(30, PSTR("or switch off"));
}

/* Warning: This function is called from interrupt context */
void lcd_buttons_update()
{
    lcd_lib_buttons_update_interrupt();
}

#endif//ENABLE_ULTILCD2
