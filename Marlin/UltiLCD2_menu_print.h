#ifndef ULTI_LCD2_MENU_PRINT_H
#define ULTI_LCD2_MENU_PRINT_H

#include "cardreader.h"
#include "SdFatConfig.h"

#define LCD_CACHE_COUNT 6
#define LCD_DETAIL_CACHE_SIZE (5+4*EXTRUDERS)
#define LCD_CACHE_SIZE (1 + (2 + LONG_FILENAME_LENGTH) * LCD_CACHE_COUNT + LCD_DETAIL_CACHE_SIZE)


#define RESUME_STATE_NORMAL 0x00
#define RESUME_STATE_RESUME 0x01
#define RESUME_STATE_MANUAL 0x02
#define RESUME_STATE_PAUSE  0x04

#ifdef FilamentDetection
#define RESUME_STATE_FILAMENT 0x08
#endif

extern uint8_t resumeState;

extern float SDUPSCurrentPosition[NUM_AXIS];
extern float SDUPSFeedrate;

extern uint8_t lcd_cache[LCD_CACHE_SIZE];
#ifdef FilamentDetection
extern boolean isFilamentDetectionEnable;
#endif

void lcd_clear_cache();

void lcd_menu_print_select();
void lcd_menu_print_resume_option();
void abortPrint();

bool readPrimed();
void clearPrimed();
void setPrimed();

void doPausePrint();


#endif//ULTI_LCD2_MENU_PRINT_H
