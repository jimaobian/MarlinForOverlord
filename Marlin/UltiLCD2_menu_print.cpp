#include <avr/pgmspace.h>

#include "Configuration.h"
#ifdef ENABLE_ULTILCD2
#include "Marlin.h"
#include "cardreader.h"
#include "temperature.h"
#include "lifetime_stats.h"
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "UltiLCD2_menu_print.h"
#include "UltiLCD2_menu_material.h"
#include "UltiLCD2_menu_maintenance.h"
#include "SDUPS.h"

uint8_t lcd_cache[LCD_CACHE_SIZE];
#define LCD_CACHE_NR_OF_FILES() lcd_cache[(LCD_CACHE_COUNT*(LONG_FILENAME_LENGTH+2))]
#define LCD_CACHE_ID(n) lcd_cache[(n)]
#define LCD_CACHE_FILENAME(n) ((char*)&lcd_cache[2*LCD_CACHE_COUNT + (n) * LONG_FILENAME_LENGTH])
#define LCD_CACHE_TYPE(n) lcd_cache[LCD_CACHE_COUNT + (n)]
#define LCD_DETAIL_CACHE_START ((LCD_CACHE_COUNT*(LONG_FILENAME_LENGTH+2))+1)
#define LCD_DETAIL_CACHE_ID() lcd_cache[LCD_DETAIL_CACHE_START]
#define LCD_DETAIL_CACHE_TIME() (*(uint32_t*)&lcd_cache[LCD_DETAIL_CACHE_START+1])
#define LCD_DETAIL_CACHE_MATERIAL(n) (*(uint32_t*)&lcd_cache[LCD_DETAIL_CACHE_START+5+4*n])

static void lcd_menu_print_out_of_filament();
static void doOutOfFilament();
static void checkPrintFinished();
static void lcd_menu_print_resume_error();
static void lcd_menu_print_resume_manual_search_sd_card();
static void lcd_menu_print_resume_manual_height();
static void doResumeManualStoreZ();
static void lcd_menu_print_resume_search_sd_card();
static void lcd_menu_print_resume_manual_option();
static void doResumeManualInit();
static void doResumeInit();
static void lcd_menu_print_ready_cooled_down();
static void lcd_menu_print_ready();
static void postPrintReady();
static void lcd_menu_print_classic_warning_again();
static void lcd_menu_print_classic_warning();
static void lcd_menu_print_error();
static void lcd_menu_print_abort();
static void lcd_menu_print_tune_retraction();
static void lcd_retraction_details(uint8_t nr);
static char* lcd_retraction_item(uint8_t nr);
static void lcd_menu_print_tune_heatup_nozzle1();
static void lcd_menu_print_tune_heatup_nozzle0();
static void lcd_menu_print_tune();
static void tune_item_details_callback(uint8_t nr);
static char* tune_item_callback(uint8_t nr);
static void lcd_menu_print_printing();
static void lcd_menu_print_heatup();
static void doStartPrint();
void lcd_menu_print_select();
static void lcd_sd_menu_details_callback(uint8_t nr);
static char* lcd_sd_menu_filename_callback(uint8_t nr);
static void cardUpdir();
void lcd_menu_print_resume_option();
static void doResumeStateResume();
static void doResumeStateNormal();

static void lcd_menu_print_prepare_printing();
static void lcd_menu_print_resume_manual_search_sd_card_eeprom();

float SDUPSCurrentPosition[NUM_AXIS];
float SDUPSFeedrate;
static float SDUPSLastZ;
uint32_t SDUPSFilePosition;
int8_t SDUPSPositionIndex;

uint8_t resumeState=RESUME_STATE_NORMAL;

#define SDUPSStateFindNone 0x00
#define SDUPSStateFindX 0x01
#define SDUPSStateFindY 0x02
#define SDUPSStateFindZ 0x04
#define SDUPSStateFindE 0x08
#define SDUPSStateFindF 0x10
#define SDUPSStateFindCustom 0x20

static uint8_t SDUPSState;

#define RESUME_ERROR_NONE 0
#define RESUME_ERROR_Z_RANGE 1
#define RESUME_ERROR_M25 2
#define RESUME_ERROR_SDUPSState 3
#define RESUME_ERROR_SD_VALIDATE 4
#define RESUME_ERROR_SD_FILEPOSITION 5
#define RESUME_ERROR_SD_READ_CARD 6
#define RESUME_ERROR_SD_SEARCH_TOO_LONG 7

static uint8_t resumeError=RESUME_ERROR_NONE;


#ifdef FilamentDetection
boolean isFilamentDetectionEnable;
#endif

void setPrimed()
{
    eeprom_write_byte((uint8_t*)EEPROM_PRIMED_OFFSET, 'H');
}

void clearPrimed()
{
    eeprom_write_byte((uint8_t*)EEPROM_PRIMED_OFFSET, 'L');
}

bool readPrimed()
{
    return (eeprom_read_byte((const uint8_t*)EEPROM_PRIMED_OFFSET) == 'H');
}

void lcd_clear_cache()
{
    for(uint8_t n=0; n<LCD_CACHE_COUNT; n++)
        LCD_CACHE_ID(n) = 0xFF;
    LCD_DETAIL_CACHE_ID() = 0;
    LCD_CACHE_NR_OF_FILES() = 0xFF;
}

void abortPrint()
{
    lcd_lib_button_up_down_reversed=false;
    postMenuCheck = NULL;
    lifetime_stats_print_end();
    doCooldown();
    discardEnqueueingCommand();
    discardCommandInBuffer();

    char buffer[32];
    if (card.sdprinting){
      card.sdprinting = false;
    }
    
    if (card.isFileOpen()) {
        card.closefile();
    }
    
    card.pause = false;
    
    if (readPrimed())
    {
        sprintf_P(buffer, PSTR("G92 E%i\nG1 F%i E0"), int(((float)END_OF_PRINT_RETRACTION) / volume_to_filament_length[active_extruder]),int(retract_feedrate));
        enquecommand(buffer);
      
        // no longer primed
        clearPrimed();
    }
  
    enquecommand_P(PSTR("G28\nM84"));
    
    isUltiGcode=true;
    feedmultiply=100;

}


/****************************************************************************************
 * Choose whether to resume
 *
 ****************************************************************************************/
static void doResumeStateNormal()
{
    resumeState=RESUME_STATE_NORMAL;
    lcd_clear_cache();
}

static void doResumeStateResume()
{
    resumeState=RESUME_STATE_RESUME;
}

void lcd_menu_print_resume_option()
{
    LED_NORMAL();
    if (!card.sdInserted)
      {
        LED_GLOW();
        lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
        lcd_info_screen(lcd_menu_main);
        lcd_lib_draw_string_centerP(15, PSTR("No SD-CARD!"));
        lcd_lib_draw_string_centerP(25, PSTR("Please insert card"));
        return;
      }
    if (!card.isOk())
      {
        lcd_info_screen(lcd_menu_main);
        lcd_lib_draw_string_centerP(16, PSTR("Reading card..."));
        return;
      }
    lcd_question_screen(lcd_menu_print_resume_manual_option, doResumeStateResume, PSTR("YES"), lcd_menu_print_select, doResumeStateNormal, PSTR("NO"),MenuForward,MenuForward);
    lcd_lib_draw_string_centerP(10, PSTR("Last print is not"));
    lcd_lib_draw_string_centerP(20, PSTR("finished. Do you"));
    lcd_lib_draw_string_centerP(30, PSTR("want to resume"));
    lcd_lib_draw_string_centerP(40, PSTR("print?"));
}


/****************************************************************************************
 * SD select
 *
 ****************************************************************************************/
static void cardUpdir()
{
    card.updir();
}

static char* lcd_sd_menu_filename_callback(uint8_t nr)
{
    //This code uses the card.longFilename as buffer to store the filename, to save memory.
    if (nr == 0)
      {
        if (card.atRoot())
          {
            strcpy_P(card.longFilename, PSTR("Return"));
          }else{
              strcpy_P(card.longFilename, PSTR("Back"));
          }
      }else{
          card.longFilename[0] = '\0';
          for(uint8_t idx=0; idx<LCD_CACHE_COUNT; idx++)
            {
              if (LCD_CACHE_ID(idx) == nr)
                  strcpy(card.longFilename, LCD_CACHE_FILENAME(idx));
            }
          if (card.longFilename[0] == '\0')
            {
              card.getfilename(nr - 1);
              if (!card.longFilename[0])
                  strcpy(card.longFilename, card.filename);
              if (!card.filenameIsDir)
                {
                  if (strchr(card.longFilename, '.')) strrchr(card.longFilename, '.')[0] = '\0';
                }
              
              uint8_t idx = nr % LCD_CACHE_COUNT;
              LCD_CACHE_ID(idx) = nr;
              strcpy(LCD_CACHE_FILENAME(idx), card.longFilename);
              LCD_CACHE_TYPE(idx) = card.filenameIsDir ? 1 : 0;
              if (card.errorCode() && card.sdInserted)
                {
                  //On a read error reset the file position and try to keep going. (not pretty, but these read errors are annoying as hell)
#ifdef ClearError
                  card.clearError();
#endif
                  LCD_CACHE_ID(idx) = 255;
                  card.longFilename[0] = '\0';
                }
            }
      }
    return card.longFilename;
}

static void lcd_sd_menu_details_callback(uint8_t nr)
{
    if (nr == 0)
      {
        return;
      }
    for(uint8_t idx=0; idx<LCD_CACHE_COUNT; idx++)
      {
        if (LCD_CACHE_ID(idx) == nr)
          {
            if (LCD_CACHE_TYPE(idx) == 1)
              {
                lcd_lib_draw_string_centerP(57, PSTR("Folder"));
              }else{
                  char buffer[64];
                  if (LCD_DETAIL_CACHE_ID() != nr)
                    {
                      card.getfilename(nr - 1);
                      if (card.errorCode())
                        {
#ifdef ClearError
                          card.clearError();
#endif
                          return;
                        }
                      LCD_DETAIL_CACHE_ID() = nr;
                      LCD_DETAIL_CACHE_TIME() = 0;
                      for(uint8_t e=0; e<EXTRUDERS; e++)
                          LCD_DETAIL_CACHE_MATERIAL(e) = 0;
                      card.openFile(card.filename, true);
                      if (card.isFileOpen())
                        {
                          for(uint8_t n=0;n<8;n++)
                            {
                              card.fgets(buffer, sizeof(buffer));
                              buffer[sizeof(buffer)-1] = '\0';
                              while (strlen(buffer) > 0 && buffer[strlen(buffer)-1] < ' ') buffer[strlen(buffer)-1] = '\0';
                              if (strncmp_P(buffer, PSTR(";TIME:"), 6) == 0)
                                  LCD_DETAIL_CACHE_TIME() = atol(buffer + 6);
                              else if (strncmp_P(buffer, PSTR(";MATERIAL:"), 10) == 0)
                                  LCD_DETAIL_CACHE_MATERIAL(0) = atol(buffer + 10);
#if EXTRUDERS > 1
                              else if (strncmp_P(buffer, PSTR(";MATERIAL2:"), 11) == 0)
                                  LCD_DETAIL_CACHE_MATERIAL(1) = atol(buffer + 11);
#endif
                            }
                          card.closefile();
                        }
                      if (card.errorCode())
                        {
                          //On a read error reset the file position and try to keep going. (not pretty, but these read errors are annoying as hell)
#ifdef ClearError
                          card.clearError();
#endif
                          LCD_DETAIL_CACHE_ID() = 255;
                        }
                    }
                  
                  if (LCD_DETAIL_CACHE_TIME() > 0)
                    {
                      char* c = buffer;
                      if (led_glow_dir)
                        {
                          strcpy_P(c, PSTR("Time: ")); c += 6;
                          c = int_to_time_string(LCD_DETAIL_CACHE_TIME(), c);
                        }else{
                            strcpy_P(c, PSTR("Material: ")); c += 10;
                            float length = float(LCD_DETAIL_CACHE_MATERIAL(0)) / (M_PI * (material[0].diameter / 2.0) * (material[0].diameter / 2.0));
                            if (length < 10000)
                                c = float_to_string(length / 1000.0, c, PSTR("m"));
                            else
                                c = int_to_string(length / 1000.0, c, PSTR("m"));
#if EXTRUDERS > 1
                            if (LCD_DETAIL_CACHE_MATERIAL(1))
                              {
                                *c++ = '/';
                                float length = float(LCD_DETAIL_CACHE_MATERIAL(1)) / (M_PI * (material[1].diameter / 2.0) * (material[1].diameter / 2.0));
                                if (length < 10000)
                                    c = float_to_string(length / 1000.0, c, PSTR("m"));
                                else
                                    c = int_to_string(length / 1000.0, c, PSTR("m"));
                              }
#endif
                        }
                      
                      lcd_draw_detail(buffer);
                    }else{
                        lcd_draw_detailP(PSTR("No info available"));
                    }
              }
          }
      }
}

void lcd_menu_print_select()
{
    LED_NORMAL();
    if (!card.sdInserted)
      {
        LED_GLOW();
        lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
        lcd_info_screen(lcd_menu_main);
        lcd_lib_draw_string_centerP(15, PSTR("No SD-CARD!"));
        lcd_lib_draw_string_centerP(25, PSTR("Please insert card"));
        lcd_clear_cache();
        return;
      }
    if (!card.isOk())
      {
        lcd_info_screen(lcd_menu_main);
        lcd_lib_draw_string_centerP(16, PSTR("Reading card..."));
        lcd_clear_cache();
        return;
      }
    
    if (LCD_CACHE_NR_OF_FILES() == 0xFF)
        LCD_CACHE_NR_OF_FILES() = card.getnrfilenames();
    if (card.errorCode())
      {
        LCD_CACHE_NR_OF_FILES() = 0xFF;
        return;
      }
    uint8_t nrOfFiles = LCD_CACHE_NR_OF_FILES();
    if (nrOfFiles == 0)
      {
        if (card.atRoot())
            lcd_info_screen(lcd_menu_main, NULL, PSTR("OK"));
        else
            lcd_info_screen(lcd_menu_print_select, cardUpdir, PSTR("OK"));
        lcd_lib_draw_string_centerP(25, PSTR("No files found!"));
        lcd_clear_cache();
        return;
      }
    
    if (lcd_lib_button_pressed)
      {
        uint8_t selIndex = uint16_t(SELECTED_SCROLL_MENU_ITEM());
        if (selIndex == 0)
          {
            if (card.atRoot())
              {
                lcd_change_to_menu(lcd_menu_main, SCROLL_MENU_ITEM_POS(0), MenuBackward);
              }else{
                  lcd_clear_cache();
                  lcd_lib_beep();
                  card.updir();
                  
                  lcd_change_to_menu(lcd_menu_print_select,SCROLL_MENU_ITEM_POS(0),MenuBackward);
              }
          }else{
              card.getfilename(selIndex - 1);
              if (!card.filenameIsDir)
                {
                  //Start print
                  active_extruder = 0;
                  card.openFile(card.filename, true);
                  if (card.isFileOpen() && !is_command_queued() && !isCommandInBuffer())
                    {
                      if (led_mode == LED_MODE_WHILE_PRINTING || led_mode == LED_MODE_BLINK_ON_DONE)
                          analogWrite(LED_PIN, 255 * int(led_brightness_level) / 100);
                      if (!card.longFilename[0])
                          strcpy(card.longFilename, card.filename);
                      card.longFilename[20] = '\0';
                      if (strchr(card.longFilename, '.')) strchr(card.longFilename, '.')[0] = '\0';
                      
                      char buffer[32];
                      card.fgets(buffer, sizeof(buffer));
                      
                      if (strncmp_P(buffer, PSTR(";FLAVOR:UltiGCode"), 17) != 0) {
                          card.fgets(buffer, sizeof(buffer));
                      }
                      
                      card.setIndex(0);
                      if (strncmp_P(buffer, PSTR(";FLAVOR:UltiGCode"), 17) == 0)
                        {
                          //New style GCode flavor without start/end code.
                          // Temperature settings, filament settings, fan settings, start and end-code are machine controlled.
                          isUltiGcode=true;
                          target_temperature_bed = 0;
                          fanSpeedPercent = 0;
                          for(uint8_t e=0; e<EXTRUDERS; e++)
                            {
                              if (LCD_DETAIL_CACHE_MATERIAL(e) < 1)
                                  continue;
                              target_temperature[e] = 0;//material[e].temperature;
#if TEMP_SENSOR_BED != 0
                              target_temperature_bed = max(target_temperature_bed, material[e].bed_temperature);
#else
                              target_temperature_bed = 0;
#endif
                              fanSpeedPercent = max(fanSpeedPercent, material[0].fan_speed);
                              volume_to_filament_length[e] = 1.0 / (M_PI * (material[e].diameter / 2.0) * (material[e].diameter / 2.0));
                              extrudemultiply[e] = material[e].flow;
                            }
                          
                          fanSpeed = 0;
                          resumeState=RESUME_STATE_NORMAL;
                          lcd_change_to_menu(lcd_menu_print_heatup);
                        }else{
                            //Classic gcode file
                            isUltiGcode=false;
                            
                            //Set the settings to defaults so the classic GCode has full control
                            fanSpeedPercent = 100;
                            for(uint8_t e=0; e<EXTRUDERS; e++)
                              {
                                volume_to_filament_length[e] = 1.0;
                                extrudemultiply[e] = 100;
                              }
                            
                            lcd_change_to_menu(lcd_menu_print_classic_warning, MAIN_MENU_ITEM_POS(0));
                        }
                    }
                }else{
                    lcd_lib_beep();
                    lcd_clear_cache();
                    card.chdir(card.filename);
                    lcd_change_to_menu(lcd_menu_print_select,SCROLL_MENU_ITEM_POS(0),MenuForward);
                }
              return;//Return so we do not continue after changing the directory or selecting a file. The nrOfFiles is invalid at this point.
          }
      }
    lcd_scroll_menu(PSTR("SD CARD"), nrOfFiles+1, lcd_sd_menu_filename_callback, lcd_sd_menu_details_callback);
    
}

/****************************************************************************************
 * heating up
 *
 ****************************************************************************************/

#define PREPARE_PRINTING_INIT 0
#define PREPARE_PRINTING_CLEAR_SDUPS_POSITION 1
#define PREPARE_PRINTING_CLEAR_SDUPS_POSITION_VALIDATE 2
#define PREPARE_PRINTING_STORE_SD_CLASS 3
#define PREPARE_PRINTING_STORE_SD_CLASS_VALIDATE 4
#define PREPARE_PRINTING_STORE_LCD_CACHE 5
#define PREPARE_PRINTING_STORE_FINISH 6

static uint8_t preparePrintingState=PREPARE_PRINTING_INIT;

static void doHotendHome()
{
    enquecommand_P(PSTR("G28"));
    preparePrintingState=PREPARE_PRINTING_INIT;
    starttime = millis();
    pausetime=(unsigned long)LCD_DETAIL_CACHE_TIME() * card.getFilePos() / card.getFileSize() *1000;
}

static void lcd_menu_print_heatup()
{
    LED_GLOW_HEAT();
    
    static unsigned long heatupTimer=millis();
    //    lcd_question_screen(lcd_menu_print_tune, NULL, PSTR("TUNE"), lcd_menu_print_abort, NULL, PSTR("ABORT"));
    
    lcd_info_screen(lcd_menu_print_abort, NULL, PSTR("ABORT"), MenuForward);
  
  
#if TEMP_SENSOR_BED == 0
  for(uint8_t e=0; e<EXTRUDERS; e++)
  {
    if (LCD_DETAIL_CACHE_MATERIAL(e) < 1 || target_temperature[e] > 0)
      continue;
    target_temperature[e] = material[e].temperature;
  }
  

    for(uint8_t e=0; e<EXTRUDERS; e++)
    {
      if (current_temperature[e] < target_temperature[e] - TEMP_HYSTERESIS || current_temperature[e] > target_temperature[e] + TEMP_HYSTERESIS) {
        heatupTimer=millis();
      }
    }
    
    if (millis()-heatupTimer>=TEMP_RESIDENCY_TIME*1000UL && printing_state == PRINT_STATE_NORMAL)
    {
      doHotendHome();
      lcd_change_to_menu(lcd_menu_print_prepare_printing, 0, MenuForward);
    }
  
#else
  
    if (current_temperature_bed > target_temperature_bed - 10)
      {
        for(uint8_t e=0; e<EXTRUDERS; e++)
          {
            if (LCD_DETAIL_CACHE_MATERIAL(e) < 1 || target_temperature[e] > 0)
                continue;
            target_temperature[e] = material[e].temperature;
          }
        
        if (current_temperature_bed >= target_temperature_bed - TEMP_WINDOW * 2 && !is_command_queued() && !isCommandInBuffer())
          {
            for(uint8_t e=0; e<EXTRUDERS; e++)
              {
                if (current_temperature[e] < target_temperature[e] - TEMP_HYSTERESIS || current_temperature[e] > target_temperature[e] + TEMP_HYSTERESIS) {
                    heatupTimer=millis();
                }
              }
            
            if (millis()-heatupTimer>=TEMP_RESIDENCY_TIME*1000UL && printing_state == PRINT_STATE_NORMAL)
              {
                doHotendHome();
                lcd_change_to_menu(lcd_menu_print_prepare_printing, 0, MenuForward);
              }
          }
      }
#endif

    uint8_t progress = 125;
    for(uint8_t e=0; e<EXTRUDERS; e++)
      {
        if (LCD_DETAIL_CACHE_MATERIAL(e) < 1 || target_temperature[e] < 1)
            continue;
        if (current_temperature[e] > 20)
            progress = min(progress, (current_temperature[e] - 20) * 125 / (target_temperature[e] - 20 - TEMP_WINDOW));
        else
            progress = 0;
      }
#if TEMP_SENSOR_BED != 0

    if (current_temperature_bed > 20)
        progress = min(progress, (current_temperature_bed - 20) * 125 / (target_temperature_bed - 20 - TEMP_WINDOW));
    else
        progress = 0;
#endif
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;
    
    lcd_lib_draw_string_centerP(10, PSTR("Heating up..."));
    lcd_lib_draw_string_centerP(20, PSTR("Preparing to print:"));
    lcd_lib_draw_string_center(30, card.longFilename);
    
    lcd_progressbar(progress);
}

/****************************************************************************************
 * prepare printing
 *
 ****************************************************************************************/
static void doStartPrint()
{
#ifdef SDUPS
  
  char buffer[64];
  char *bufferPtr;
  uint8_t cardErrorTimes=0;
  
  retracted = false;
  // note that we have primed, so that we know to de-prime at the end
  
  current_position[E_AXIS]=0;
  plan_set_e_position(current_position[E_AXIS]);
  
    if (resumeState) {
        isUltiGcode=true;

        bufferPtr=buffer;
        
        strcpy_P(bufferPtr, PSTR("G1 F6000 Z"));
        bufferPtr+=strlen_P(PSTR("G1 F6000 Z"));
        
        bufferPtr=float_to_string(min(SDUPSCurrentPosition[Z_AXIS]+PRIMING_HEIGHT+50, 260), bufferPtr);
        
        strcpy_P(bufferPtr, PSTR("\nG1 X0 Y"Y_MIN_POS_STR" Z"));
        bufferPtr+=strlen_P(PSTR("\nG1 X0 Y"Y_MIN_POS_STR" Z"));
        
        bufferPtr=float_to_string(min(SDUPSCurrentPosition[Z_AXIS]+PRIMING_HEIGHT, 260), bufferPtr);
        
        enquecommand(buffer);
    }
    else{
        if (isUltiGcode) {
            
            SDUPSStart();
          enquecommand_P(PSTR("G1 F6000 Z50\nG1 X0 Y"Y_MIN_POS_STR" Z" PRIMING_HEIGHT_STRING));
        }
    }
  
  for(uint8_t e = 0; e<EXTRUDERS; e++)
  {
    if (!LCD_DETAIL_CACHE_MATERIAL(e))
      continue;
      
    bufferPtr=buffer;
      
    if (!readPrimed()) {
        
        if (e>0) {
            //Todo
        }
        else{
            strcpy_P(bufferPtr, PSTR("G92 E"));
            bufferPtr+=strlen_P(PSTR("G92 E"));
            
            bufferPtr=int_to_string((0.0 - END_OF_PRINT_RETRACTION) / volume_to_filament_length[e] - PRIMING_MM3, bufferPtr);
            
            strcpy_P(bufferPtr, PSTR("\nG1 F"));
            bufferPtr+=strlen_P(PSTR("\nG1 F"));
            
            bufferPtr=int_to_string(END_OF_PRINT_RECOVERY_SPEED*60, bufferPtr);
            
            strcpy_P(bufferPtr, PSTR(" E-"PRIMING_MM3_STRING"\nG1 F"));
            bufferPtr+=strlen_P(PSTR(" E-"PRIMING_MM3_STRING"\nG1 F"));
            
            bufferPtr=int_to_string(PRIMING_MM3_PER_SEC * volume_to_filament_length[e]*60, bufferPtr);
            
            strcpy_P(bufferPtr, PSTR(" E0\n"));
            bufferPtr+=strlen_P(PSTR(" E0\n"));
        }
      setPrimed();
    }
      
    else{
        if (e>0) {
            //Todo
        }
        else{
            strcpy_P(bufferPtr, PSTR("G92 E-"PRIMING_MM3_STRING"\nG1 F"));
            bufferPtr+=strlen_P(PSTR("G92 E-"PRIMING_MM3_STRING"\nG1 F"));
            
            bufferPtr=int_to_string(PRIMING_MM3_PER_SEC * volume_to_filament_length[e]*60, bufferPtr);
            
            strcpy_P(bufferPtr, PSTR(" E0\n"));
            bufferPtr+=strlen_P(PSTR(" E0\n"));
        }
    }
      enquecommand(buffer);
  }
    
    
    if (resumeState) {
      
      bufferPtr=buffer;
      
      strcpy_P(bufferPtr, PSTR("G92 E"));
      bufferPtr+=strlen_P(PSTR("G92 E"));
      
      ltoa(long(SDUPSCurrentPosition[E_AXIS]), bufferPtr, 10);
      bufferPtr=buffer+strlen(buffer);
      
      
      strcpy_P(bufferPtr, PSTR("\nG1 F3000 X"));
      bufferPtr+=strlen_P(PSTR("\nG1 F3000 X"));
      
      bufferPtr=float_to_string(SDUPSCurrentPosition[X_AXIS], bufferPtr);
      
      strcpy_P(bufferPtr, PSTR(" Y"));
      bufferPtr+=strlen_P(PSTR(" Y"));
      
      bufferPtr=float_to_string(SDUPSCurrentPosition[Y_AXIS], bufferPtr);
      
      strcpy_P(bufferPtr, PSTR(" Z"));
      bufferPtr+=strlen_P(PSTR(" Z"));
      
      bufferPtr=float_to_string(SDUPSCurrentPosition[Z_AXIS], bufferPtr);
      
        if (SDUPSFeedrate>0.0) {
            strcpy_P(bufferPtr, PSTR("\nG1 F"));
            bufferPtr+=strlen_P(PSTR("\nG1 F"));
            
            bufferPtr=int_to_string(SDUPSFeedrate, bufferPtr);
        }
        
        
      enquecommand(buffer);
        
      enquecommand_P(PSTR("M107\nM220 S40\nM780 S255\nM781 S100"));
      
    }else{
        enquecommand_P(PSTR("G1 F3000 Z0\nG1 F400 Z20"));
    }
  
  postMenuCheck = checkPrintFinished;
  card.startFileprint();
#endif
}



static void lcd_menu_print_prepare_printing()
{
  
  LED_PRINT();
  lcd_info_screen(NULL, NULL, PSTR("WAITING"));
    lcd_lib_draw_string_centerP(10, PSTR("Prepare Printing:"));
    lcd_lib_draw_string_center(20, card.longFilename);
    
    static int EEPROMSDUPSClassIndex=EEPROM_SDUPSClassOffset;
    
    uint16_t SDUPSValidate;
    uint32_t lastFilePosition;
    uint8_t cardErrorTimes;
  
  if (printing_state==PRINT_STATE_NORMAL && is_command_queued()==false && isCommandInBuffer()==false && movesplanned()==0) {
      
      if (resumeState) {
          preparePrintingState=PREPARE_PRINTING_STORE_FINISH;
      }
      
      switch (preparePrintingState) {
          case PREPARE_PRINTING_INIT:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_INIT");
              EEPROMSDUPSClassIndex=EEPROM_SDUPSClassOffset;
              preparePrintingState=PREPARE_PRINTING_CLEAR_SDUPS_POSITION;
              break;
          case PREPARE_PRINTING_CLEAR_SDUPS_POSITION:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_CLEAR_SDUPS_POSITION");
              for (int index=0; index<(SDUPSPositionSize*sizeof(uint32_t)); index++) {
                  eeprom_write_byte((uint8_t*)EEPROM_SDUPSPositionOffset+index, (uint8_t)0x00);
              }
              preparePrintingState=PREPARE_PRINTING_CLEAR_SDUPS_POSITION_VALIDATE;
              break;
          case PREPARE_PRINTING_CLEAR_SDUPS_POSITION_VALIDATE:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_CLEAR_SDUPS_POSITION_VALIDATE");
              for (int index=0; index<(SDUPSPositionSize*sizeof(uint32_t)); index++) {
                  if (eeprom_read_byte((uint8_t*)EEPROM_SDUPSPositionOffset+index)) {
                      SERIAL_ERRORLN("eeprom broken!!!");
                      SERIAL_ERRORLN(index);
                  }
              }
              preparePrintingState=PREPARE_PRINTING_STORE_SD_CLASS;
              break;
          case PREPARE_PRINTING_STORE_SD_CLASS:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_STORE_SD_CLASS");
              EEPROM_WRITE_VAR(EEPROMSDUPSClassIndex, card);
              preparePrintingState=PREPARE_PRINTING_STORE_SD_CLASS_VALIDATE;
              break;
          case PREPARE_PRINTING_STORE_SD_CLASS_VALIDATE:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_STORE_SD_CLASS_VALIDATE");
              lastFilePosition=card.getFilePos();
              cardErrorTimes=0;
              do {
                  card.clearError();
                  cardErrorTimes++;
                  SDUPSValidate=0;
                  card.setIndex(card.getFileSize()/2UL);
                  
                  for (int j=0; j<SDUPSValidateSize; j++) {
                      SDUPSValidate += (uint8_t)(card.get());
                  }
              } while (card.errorCode() && cardErrorTimes<5);
              EEPROM_WRITE_VAR(EEPROMSDUPSClassIndex, SDUPSValidate);
              card.setIndex(lastFilePosition);
              preparePrintingState=PREPARE_PRINTING_STORE_LCD_CACHE;
              break;
          case PREPARE_PRINTING_STORE_LCD_CACHE:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_STORE_LCD_CACHE");
              EEPROM_WRITE_VAR(EEPROMSDUPSClassIndex, lcd_cache);
              preparePrintingState=PREPARE_PRINTING_STORE_FINISH;
              break;
          case PREPARE_PRINTING_STORE_FINISH:
              SERIAL_DEBUGLNPGM("PREPARE_PRINTING_STORE_FINISH");
              doStartPrint();
              currentMenu=lcd_menu_print_printing;
              break;
          default:
              break;
      }
  }

  char buffer[18];
  

  float printTimeMs = (millis() - starttime + pausetime);
  float printTimeSec = printTimeMs / 1000L;
  
  int_to_time_string(printTimeSec, buffer);
  lcd_lib_draw_string_center_at(25, 30, buffer);
  
  unsigned long timeLeftSec = LCD_DETAIL_CACHE_TIME() - printTimeSec;
    buffer[0]='-';
  int_to_time_string(timeLeftSec, buffer+1);
  lcd_lib_draw_string_center_at(95, 30, buffer);
  
  lcd_progressbar(0);
}

/****************************************************************************************
 * printing
 *
 ****************************************************************************************/
static void lcd_menu_print_printing()
{
  
    LED_PRINT();
    
    lcd_question_screen(lcd_menu_print_tune, NULL, PSTR("TUNE"), lcd_menu_print_abort, NULL, PSTR("PAUSE"));
    uint8_t progress = card.getFilePos() / ((card.getFileSize() + 123) / 124);
    char buffer[32];
    char* c;
    switch(printing_state)
  {
      default:
        lcd_lib_draw_string_centerP(10, PSTR("Printing:"));
        lcd_lib_draw_string_center(20, card.longFilename);
        break;
      case PRINT_STATE_WAIT_USER:
        lcd_lib_encoder_pos = 0;
        lcd_lib_draw_string_centerP(10, PSTR("Press button"));
        lcd_lib_draw_string_centerP(20, PSTR("to continue"));
        break;
        
      case PRINT_STATE_HEATING:
        lcd_lib_draw_string_centerP(10, PSTR("Heating"));
        c = int_to_string(current_temperature[0], buffer, PSTR("\x81""C"));
        *c++ = '/';
        c = int_to_string(target_temperature[0], c, PSTR("\x81""C"));
        lcd_lib_draw_string_center(20, buffer);
        break;
#if TEMP_SENSOR_BED != 0
      case PRINT_STATE_HEATING_BED:
        lcd_lib_draw_string_centerP(10, PSTR("Heating buildplate"));
        c = int_to_string(current_temperature_bed, buffer, PSTR("\x81""C"));
        *c++ = '/';
        c = int_to_string(target_temperature_bed, c, PSTR("\x81""C"));
        lcd_lib_draw_string_center(20, buffer);
        break;
#endif
  }
    float printTimeMs = (millis() - starttime + pausetime);
    float printTimeSec = printTimeMs / 1000L;
  
    unsigned long timeLeftSec;
    
    float totalTimeMs = float(printTimeMs) * float(card.getFileSize()) / float(card.getFilePos());
    static float totalTimeSmoothSec;
    totalTimeSmoothSec = (totalTimeSmoothSec * 999L + totalTimeMs / 1000L) / 1000L;
    if (isinf(totalTimeSmoothSec))
        totalTimeSmoothSec = totalTimeMs;
    
    int_to_time_string(printTimeSec, buffer);
    lcd_lib_draw_string_center_at(25, 30, buffer);
    
    if (LCD_DETAIL_CACHE_TIME() == 0 && printTimeSec < 60*3)
      {
        totalTimeSmoothSec = totalTimeMs / 1000;
        
        lcd_lib_draw_string_center_atP(95, 30, PSTR("--:--:--"));
      }else{
          unsigned long totalTimeSec;
          
          if (totalTimeSmoothSec>(5*LCD_DETAIL_CACHE_TIME())) {
              totalTimeSmoothSec=LCD_DETAIL_CACHE_TIME();
          }
          
          if (printTimeSec<(LCD_DETAIL_CACHE_TIME() / 2)) {
              totalTimeSec=LCD_DETAIL_CACHE_TIME();
          }
          else{
              float f = (float(LCD_DETAIL_CACHE_TIME() / 2) / float(printTimeSec))*2.0 - 1;
              f=constrain(f, 0.0, 1.0);
              totalTimeSec = float(totalTimeSmoothSec) * (1-f) + float(LCD_DETAIL_CACHE_TIME()) * (f);
              totalTimeSec = totalTimeSmoothSec;
            }
          
          int_to_time_string(totalTimeSec - printTimeSec, buffer+1);
          buffer[0]='-';
          lcd_lib_draw_string_center_at(95, 30, buffer);
      }
  
  
  
  if (isBLEUpdate && millis()-isBLEUpdateTimer>30) {
    switch (isBLEUpdate) {
      case 3:
        sprintf_P(buffer, PSTR("M772 P%i\r\n"),int(card.getFilePos()*100 / (card.getFileSize())));
        SERIAL_BLE_PROTOCOL(buffer);
        isBLEUpdateTimer=millis();
        isBLEUpdate=2;
        break;
      case 2:
        sprintf_P(buffer, PSTR("M773 S%ld\r\n"),long(printTimeSec));
        SERIAL_BLE_PROTOCOL(buffer);
        isBLEUpdateTimer=millis();
        isBLEUpdate=1;
        break;
      case 1:
        sprintf_P(buffer, PSTR("M774 S%ld\r\n"),long(timeLeftSec));
        SERIAL_BLE_PROTOCOL(buffer);
        isBLEUpdateTimer=millis();
        isBLEUpdate=0;
        break;
      default:
        break;
    }
  }
  
    lcd_progressbar(progress);
}

    /****************************************************************************************
     * tune
     *
     ****************************************************************************************/
static char* tune_item_callback(uint8_t nr)
{
    char* c = (char*)lcd_cache;
    if (nr == 0)
        strcpy_P(c, PSTR("Return"));
//    else if (nr == 1)
//      {
//        if (!card.pause)
//          {
//            if (movesplanned() > 0)
//                strcpy_P(c, PSTR("Pause"));
//            else
//              {
//                //                if (cardDebug) {
//                //                    strcpy_P(c, PSTR("sd error"));
//                //                }
//                //                else{
//                strcpy_P(c, PSTR("Can not pause"));
//                //                }
//              }
//          }
//        else
//          {
//            if (movesplanned() < 1)
//                strcpy_P(c, PSTR("Resume"));
//            else
//                strcpy_P(c, PSTR("Pausing..."));
//          }
//      }
    else if (nr == 1)
        strcpy_P(c, PSTR("Speed"));
    else if (nr == 2)
        strcpy_P(c, PSTR("Temperature"));
#if EXTRUDERS > 1
    else if (nr == 3)
        strcpy_P(c, PSTR("Temperature 2"));
#endif
    else if (nr == 2 + EXTRUDERS)
        strcpy_P(c, PSTR("Buildplate temp."));
    else if (nr == 3 + EXTRUDERS)
        strcpy_P(c, PSTR("Fan speed"));
    else if (nr == 4 + EXTRUDERS)
        strcpy_P(c, PSTR("Material flow"));
#if EXTRUDERS > 1
    else if (nr == 5 + EXTRUDERS)
        strcpy_P(c, PSTR("Material flow 2"));
#endif
    else if (nr == 4 + EXTRUDERS * 2)
        strcpy_P(c, PSTR("Retraction"));
    return c;
}

static void tune_item_details_callback(uint8_t nr)
{
#if TEMP_SENSOR_BED != 0

  char* c = (char*)lcd_cache;
  if (nr == 1)
    c = int_to_string(feedmultiply, c, PSTR("%"));
  else if (nr == 2)
  {
    c = int_to_string(current_temperature[0], c, PSTR("\x81""C"));
    *c++ = '/';
    c = int_to_string(target_temperature[0], c, PSTR("\x81""C"));
  }
#if EXTRUDERS > 1
  else if (nr == 3)
  {
    c = int_to_string(current_temperature[1], c, PSTR("\x81""C"));
    *c++ = '/';
    c = int_to_string(target_temperature[1], c, PSTR("\x81""C"));
  }
#endif
  else if (nr == 2 + EXTRUDERS)
  {
    c = int_to_string(current_temperature_bed, c, PSTR("\x81""C"));
    *c++ = '/';
    
#ifdef FilamentDetection
    if (FilamentAvailable()) {
      c = int_to_string(target_temperature_bed, c, PSTR("\x81""C  Yes"));
    }
    else{
      c = int_to_string(target_temperature_bed, c, PSTR("\x81""C  No"));
    }
    
#else
    c = int_to_string(target_temperature_bed, c, PSTR("\x81""C"));
#endif
  }
  else if (nr == 3 + EXTRUDERS)
    c = int_to_string(lround((int(fanSpeed) * 100) / 255.0), c, PSTR("%"));
  else if (nr == 4 + EXTRUDERS)
    c = int_to_string(extrudemultiply[0], c, PSTR("%"));
#if EXTRUDERS > 1
  else if (nr == 5 + EXTRUDERS)
    c = int_to_string(extrudemultiply[1], c, PSTR("%"));
#endif
  else
    return;
  lcd_draw_detail((char*)lcd_cache);
  //    lcd_lib_draw_string(5, 57, (char*)lcd_cache);
  
#else
  char* c = (char*)lcd_cache;
  if (nr == 1)
    c = int_to_string(feedmultiply, c, PSTR("%"));
  else if (nr == 2)
  {
    c = int_to_string(current_temperature[0], c, PSTR("\x81""C"));
    *c++ = '/';
    c = int_to_string(target_temperature[0], c, PSTR("\x81""C"));
  }
#if EXTRUDERS > 1
  else if (nr == 3)
  {
    c = int_to_string(current_temperature[1], c, PSTR("\x81""C"));
    *c++ = '/';
    c = int_to_string(target_temperature[1], c, PSTR("\x81""C"));
  }
#endif
  else if (nr == 2 + EXTRUDERS)
    c = int_to_string(lround((int(fanSpeed) * 100) / 255.0), c, PSTR("%"));
  else if (nr == 3 + EXTRUDERS)
    c = int_to_string(extrudemultiply[0], c, PSTR("%"));
#if EXTRUDERS > 1
  else if (nr == 4 + EXTRUDERS)
    c = int_to_string(extrudemultiply[1], c, PSTR("%"));
#endif
  else
    return;
  lcd_draw_detail((char*)lcd_cache);
  //    lcd_lib_draw_string(5, 57, (char*)lcd_cache);
#endif
  
  

}

static void lcd_menu_print_tune()
{
    LED_PRINT();
    lcd_scroll_menu(PSTR("TUNE"), 5 + EXTRUDERS * 2, tune_item_callback, tune_item_details_callback);
    if (lcd_lib_button_pressed)
      {
#if TEMP_SENSOR_BED != 0
        
        if (IS_SELECTED_SCROLL(0))
        {
          if (card.sdprinting)
            lcd_change_to_menu(lcd_menu_print_printing, MAIN_MENU_ITEM_POS(0), MenuBackward);
          else
            lcd_change_to_menu(lcd_menu_print_heatup, MAIN_MENU_ITEM_POS(0), MenuBackward);
        }
        else if (IS_SELECTED_SCROLL(1))
        {
          targetFeedmultiply=0;
          LCD_EDIT_SETTING(feedmultiply, "Print speed", "%", 10, 1000);
        }
        else if (IS_SELECTED_SCROLL(2))
        {
          lcd_lib_button_up_down_reversed = true;
          lcd_change_to_menu(lcd_menu_print_tune_heatup_nozzle0, 0);
        }
#if EXTRUDERS > 1
        else if (IS_SELECTED_SCROLL(3))
          lcd_change_to_menu(lcd_menu_print_tune_heatup_nozzle1, 0);
#endif
        else if (IS_SELECTED_SCROLL(2 + EXTRUDERS))
        {
          lcd_change_to_menu(lcd_menu_maintenance_advanced_bed_heatup, 0);//Use the maintainace heatup menu, which shows the current temperature.
          lcd_lib_button_up_down_reversed = true;
        }
        else if (IS_SELECTED_SCROLL(3 + EXTRUDERS))
        {
          targetFanSpeed=0;
          LCD_EDIT_SETTING_BYTE_PERCENT(fanSpeed, "Fan speed", "%", 0, 100);
        }
        else if (IS_SELECTED_SCROLL(4 + EXTRUDERS))
          LCD_EDIT_SETTING(extrudemultiply[0], "Material flow", "%", 10, 1000);
#if EXTRUDERS > 1
        else if (IS_SELECTED_SCROLL(5 + EXTRUDERS))
          LCD_EDIT_SETTING(extrudemultiply[1], "Material flow 2", "%", 10, 1000);
#endif
        else if (IS_SELECTED_SCROLL(4 + EXTRUDERS * 2))
          lcd_change_to_menu(lcd_menu_print_tune_retraction);
        
#else
        
        if (IS_SELECTED_SCROLL(0))
        {
          if (card.sdprinting)
            lcd_change_to_menu(lcd_menu_print_printing, MAIN_MENU_ITEM_POS(0), MenuBackward);
          else
            lcd_change_to_menu(lcd_menu_print_heatup, MAIN_MENU_ITEM_POS(0), MenuBackward);
        }
        else if (IS_SELECTED_SCROLL(1))
        {
          targetFeedmultiply=0;
          LCD_EDIT_SETTING(feedmultiply, "Print speed", "%", 10, 1000);
        }
        else if (IS_SELECTED_SCROLL(2))
        {
          lcd_lib_button_up_down_reversed = true;
          lcd_change_to_menu(lcd_menu_print_tune_heatup_nozzle0, 0);
        }
#if EXTRUDERS > 1
        else if (IS_SELECTED_SCROLL(3))
          lcd_change_to_menu(lcd_menu_print_tune_heatup_nozzle1, 0);
#endif
        else if (IS_SELECTED_SCROLL(2 + EXTRUDERS))
        {
          targetFanSpeed=0;
          LCD_EDIT_SETTING_BYTE_PERCENT(fanSpeed, "Fan speed", "%", 0, 100);
        }
        else if (IS_SELECTED_SCROLL(3 + EXTRUDERS))
          LCD_EDIT_SETTING(extrudemultiply[0], "Material flow", "%", 10, 1000);
#if EXTRUDERS > 1
        else if (IS_SELECTED_SCROLL(4 + EXTRUDERS))
          LCD_EDIT_SETTING(extrudemultiply[1], "Material flow 2", "%", 10, 1000);
#endif
        else if (IS_SELECTED_SCROLL(3 + EXTRUDERS * 2))
          lcd_change_to_menu(lcd_menu_print_tune_retraction);
#endif
      }
}

        /****************************************************************************************
         * nozzle temperature
         *
         ****************************************************************************************/
static void lcd_menu_print_tune_heatup_nozzle0()
{
    LED_PRINT();
    if (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM != 0)
      {
        target_temperature[0] = int(target_temperature[0]) + (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM);
        if (target_temperature[0] < 0)
            target_temperature[0] = 0;
        if (target_temperature[0] > HEATER_0_MAXTEMP - 15)
            target_temperature[0] = HEATER_0_MAXTEMP - 15;
        lcd_lib_encoder_pos = 0;
      }
    if (lcd_lib_button_pressed)
      {
        lcd_change_to_menu(previousMenu, previousEncoderPos, MenuBackward);
        lcd_lib_button_up_down_reversed = false;
      }
    
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(20, PSTR("Nozzle temperature:"));
    lcd_lib_draw_string_centerP(53, PSTR("Click to return"));
    char buffer[18];
    int_to_string(int(current_temperature[0]), buffer, PSTR("\x81""C/"));
    int_to_string(int(target_temperature[0]), buffer+strlen(buffer), PSTR("\x81""C"));
    lcd_lib_draw_string_center(30, buffer);
}
#if EXTRUDERS > 1
static void lcd_menu_print_tune_heatup_nozzle1()
{
    if (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM != 0)
      {
        target_temperature[1] = int(target_temperature[1]) + (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM);
        if (target_temperature[1] < 0)
            target_temperature[1] = 0;
        if (target_temperature[1] > HEATER_0_MAXTEMP - 15)
            target_temperature[1] = HEATER_0_MAXTEMP - 15;
        lcd_lib_encoder_pos = 0;
      }
    if (lcd_lib_button_pressed)
        lcd_change_to_menu(previousMenu, previousEncoderPos, false);
    
    
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(20, PSTR("Nozzle2 temperature:"));
    lcd_lib_draw_string_centerP(53, PSTR("Click to return"));
    char buffer[18];
    int_to_string(int(current_temperature[1]), buffer, PSTR("\x81""C/"));
    int_to_string(int(target_temperature[1]), buffer+strlen(buffer), PSTR("\x81""C"));
    lcd_lib_draw_string_center(30, buffer);
}
#endif

        /****************************************************************************************
         * retraction setting
         *
         ****************************************************************************************/
static char* lcd_retraction_item(uint8_t nr)
{
    if (nr == 0)
        strcpy_P((char*)lcd_cache, PSTR("Return"));
    else if (nr == 1)
        strcpy_P((char*)lcd_cache, PSTR("Retract length"));
    else if (nr == 2)
        strcpy_P((char*)lcd_cache, PSTR("Retract speed"));
#if EXTRUDERS > 1
    else if (nr == 3)
        strcpy_P((char*)lcd_cache, PSTR("Extruder change len"));
#endif
    else if (nr == EXTRUDERS+2)
        strcpy_P((char*)lcd_cache, PSTR("Retract Zlift"));
    else
        strcpy_P((char*)lcd_cache, PSTR("???"));
    return (char*)lcd_cache;
}

static void lcd_retraction_details(uint8_t nr)
{
    char buffer[16];
    if (nr == 0)
        return;
    else if(nr == 1)
        float_to_string(retract_length, buffer, PSTR("mm"));
    else if(nr == 2)
        int_to_string(retract_feedrate / 60 + 0.5, buffer, PSTR("mm/sec"));
#if EXTRUDERS > 1
    else if(nr == 3)
        int_to_string(extruder_swap_retract_length, buffer, PSTR("mm"));
#endif
    else if(nr == EXTRUDERS+2)
        float_to_string(retract_zlift, buffer, PSTR("mm"));
    lcd_draw_detail(buffer);
    //    lcd_lib_draw_string(5, 57, buffer);
}

static void lcd_menu_print_tune_retraction()
{
    LED_PRINT();
    lcd_scroll_menu(PSTR("RETRACTION"), 3 + (EXTRUDERS > 1 ? 1 : 0), lcd_retraction_item, lcd_retraction_details);
    if (lcd_lib_button_pressed)
      {
        if (IS_SELECTED_SCROLL(0))
            lcd_change_to_menu(lcd_menu_print_tune, SCROLL_MENU_ITEM_POS(6), MenuBackward);
        else if (IS_SELECTED_SCROLL(1))
            LCD_EDIT_SETTING_FLOAT001(retract_length, "Retract length", "mm", 0, 50);
        else if (IS_SELECTED_SCROLL(2))
            LCD_EDIT_SETTING_SPEED(retract_feedrate, "Retract speed", "mm/sec", 0, max_feedrate[E_AXIS] * 60);
#if EXTRUDERS > 1
        else if (IS_SELECTED_SCROLL(3))
            LCD_EDIT_SETTING_FLOAT001(extruder_swap_retract_length, "Extruder change", "mm", 0, 50);
#endif
        else if (IS_SELECTED_SCROLL(EXTRUDERS+2))
            LCD_EDIT_SETTING_FLOAT001(retract_zlift, "retract_zlift", "mm", 0, 50);
      }
}


    /****************************************************************************************
     * Pause
     *
     ****************************************************************************************/

void doPausePrint()
{
    if (card.sdprinting){
        card.sdprinting = false;
        postMenuCheck = NULL;
        SDUPSStorePosition(card.getFilePos());
    }
}


static void lcd_menu_print_abort()
{
    LED_GLOW();
    if (card.sdprinting) {
        lcd_question_screen(NULL, doPausePrint, PSTR("YES"), previousMenu, NULL, PSTR("NO"));
        lcd_lib_draw_string_centerP(20, PSTR("Pause the print?"));
    }
    else{
        lcd_info_screen(NULL,NULL,PSTR("Waiting"));
        lcd_lib_draw_string_centerP(20, PSTR("Pausing..."));
    }
    
    if (!(isCommandInBuffer()||is_command_queued()||movesplanned())) {
        lcd_change_to_menu(lcd_menu_print_ready);
        abortPrint();
    }
}

/****************************************************************************************
 * Print Error
 *
 ****************************************************************************************/

static void lcd_menu_print_error()
{
    LED_ERROR();
    lcd_info_screen(lcd_menu_main, NULL, PSTR("RETURN TO MAIN"));
    
    lcd_lib_draw_string_centerP(10, PSTR("Error while"));
    lcd_lib_draw_string_centerP(20, PSTR("reading"));
    lcd_lib_draw_string_centerP(30, PSTR("SD-card!"));
    char buffer[12];
    strcpy_P(buffer, PSTR("Code:"));
    int_to_string(card.errorCode(), buffer+5);
    lcd_lib_draw_string_center(40, buffer);
}

static void lcd_menu_print_classic_warning()
{
    LED_GLOW();
    lcd_question_screen( lcd_menu_print_select, doResumeStateNormal, PSTR("CANCEL"), lcd_menu_print_classic_warning_again, NULL, PSTR("CONTINUE"));
    
    lcd_lib_draw_string_centerP(10, PSTR("Please Use UltiGGocde"));
    lcd_lib_draw_string_centerP(20, PSTR("Cura>>Machine setting"));
    lcd_lib_draw_string_centerP(30, PSTR("GCode Flavor>>"));
    lcd_lib_draw_string_centerP(40, PSTR("UltiGcode"));
}

static void lcd_menu_print_classic_warning_again()
{
    LED_GLOW();
    lcd_question_screen( lcd_menu_print_select, doResumeStateNormal, PSTR("CANCEL"), lcd_menu_print_printing, doStartPrint, PSTR("CONTINUE"));
    
    lcd_lib_draw_string_centerP(10, PSTR("Please Use UltiGGocde"));
    lcd_lib_draw_string_centerP(20, PSTR("Cura>>Machine setting"));
    lcd_lib_draw_string_centerP(30, PSTR("GCode Flavor>>"));
    lcd_lib_draw_string_centerP(40, PSTR("UltiGcode"));
}

/****************************************************************************************
 * Print Done
 *
 ****************************************************************************************/
static void postPrintReady()
{
    if (led_mode == LED_MODE_BLINK_ON_DONE)
        analogWrite(LED_PIN, 0);
    
    fanSpeed=0;
    LED_NORMAL();
}

static void lcd_menu_print_ready()
{
    LED_GLOW_END();
    
    if (led_mode == LED_MODE_WHILE_PRINTING)
        analogWrite(LED_PIN, 0);
    else if (led_mode == LED_MODE_BLINK_ON_DONE)
        analogWrite(LED_PIN, (led_glow << 1) * int(led_brightness_level) / 100);
    lcd_info_screen(lcd_menu_main, postPrintReady, PSTR("BACK TO MENU"));
#if TEMP_SENSOR_BED != 0
    if (current_temperature[0] > 60 || current_temperature_bed > 40)
      {
        lcd_lib_draw_string_centerP(15, PSTR("Printer cooling down"));
        
        int16_t progress = 124 - (current_temperature[0] - 60);
        if (progress < 0) progress = 0;
        if (progress > 124) progress = 124;
        
        if (progress < minProgress)
            progress = minProgress;
        else
            minProgress = progress;
        
        lcd_progressbar(progress);
        char buffer[18];
        char* c = buffer;
        for(uint8_t e=0; e<EXTRUDERS; e++)
            c = int_to_string(current_temperature[e], c, PSTR("\x81""C   "));
        int_to_string(current_temperature_bed, c, PSTR("\x81""C"));
        lcd_lib_draw_string_center(25, buffer);
      }else{
          currentMenu = lcd_menu_print_ready_cooled_down;
          fanSpeed=0;
      }
#else
  if (current_temperature[0] > 60)
  {
    lcd_lib_draw_string_centerP(15, PSTR("Printer cooling down"));
    
    int16_t progress = 124 - (current_temperature[0] - 60);
    if (progress < 0) progress = 0;
    if (progress > 124) progress = 124;
    
    if (progress < minProgress)
      progress = minProgress;
    else
      minProgress = progress;
    
    lcd_progressbar(progress);
    char buffer[18];
    char* c = buffer;
    for(uint8_t e=0; e<EXTRUDERS; e++)
      c = int_to_string(current_temperature[e], c, PSTR("\x81""C   "));
    lcd_lib_draw_string_center(25, buffer);
  }else{
    currentMenu = lcd_menu_print_ready_cooled_down;
    fanSpeed=0;
  }
#endif
}

static void lcd_menu_print_ready_cooled_down()
{
    if (led_mode == LED_MODE_WHILE_PRINTING)
        analogWrite(LED_PIN, 0);
    else if (led_mode == LED_MODE_BLINK_ON_DONE)
        analogWrite(LED_PIN, (led_glow << 1) * int(led_brightness_level) / 100);
    lcd_info_screen(lcd_menu_main, postPrintReady, PSTR("BACK TO MENU"));
    
    LED_GLOW();
    
    lcd_lib_draw_string_centerP(10, PSTR("Print finished"));
    lcd_lib_draw_string_centerP(30, PSTR("You can remove"));
    lcd_lib_draw_string_centerP(40, PSTR("the model."));
}


/****************************************************************************************
 * Select Manual resume or not
 *
 ****************************************************************************************/


static bool handleResumeError(uint8_t theErrorCode)
{
  if ((resumeError=theErrorCode)!=RESUME_ERROR_NONE) {
    lcd_change_to_menu(lcd_menu_print_resume_error);
    return true;
  }
  else{
    return false;
  }
}

static uint8_t doRetriveClass()
{
    if (!SDUPSRetrieveClass()) {
        return RESUME_ERROR_SD_VALIDATE;
    }
    
    SDUPSScanPosition();
    return RESUME_ERROR_NONE;
}

static uint8_t doStableReadSD(uint32_t& theFilePosition, char *theBuffer, uint8_t theSize)
{
    uint8_t cardErrorTimes=0;

    do {
        card.clearError();
        cardErrorTimes++;
        card.setIndex(theFilePosition);
        card.fgets(theBuffer, theSize);
    } while (card.errorCode() && cardErrorTimes<5);
    
    if (card.errorCode()) {
        return RESUME_ERROR_SD_READ_CARD;
    }
    
    return RESUME_ERROR_NONE;
}

static uint8_t doSearchZLayer(uint32_t& theFilePosition, char *theBuffer, uint8_t theSize)
{
    unsigned long printResumeTimer=millis();
    
    char* searchPtr;
    
    SDUPSState=SDUPSStateFindNone;
    card.setIndex(theFilePosition);

    do {
        
        if (millis()-printResumeTimer>=2000) {
            previous_millis_cmd = millis();
            return RESUME_ERROR_SD_SEARCH_TOO_LONG;
        }
        
        card.fgets(theBuffer, theSize);
        
        if (card.errorCode())
        {
            SERIAL_ECHOLNPGM("sd error");
            if (!card.sdInserted)
            {
                return RESUME_ERROR_SD_READ_CARD;
            }
            //On an error, reset the error, reset the file position and try again.
#ifdef ClearError
            card.clearError();
#endif
            card.setIndex(card.getFilePos());
            continue;
        }
        
        if (theBuffer[0]=='G') {
            
            searchPtr=strchr(theBuffer, 'X');
            if (searchPtr!=NULL) {
                SDUPSCurrentPosition[X_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindX;
                SERIAL_DEBUGLNPGM("found x");
            }
            
            searchPtr=strchr(theBuffer, 'Y');
            if (searchPtr!=NULL) {
                SDUPSCurrentPosition[Y_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindY;
                SERIAL_DEBUGLNPGM("found y");
            }
            
            searchPtr=strchr(theBuffer, 'Z');
            if (searchPtr!=NULL) {
                SDUPSCurrentPosition[Z_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindZ;
                theFilePosition=card.getFilePos();
                SERIAL_DEBUGLNPGM("found z");
            }
            
            searchPtr=strchr(theBuffer, 'E');
            if (searchPtr!=NULL) {
                SDUPSCurrentPosition[E_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindE;
                SERIAL_DEBUGLNPGM("found e");
            }
            
            searchPtr=strchr(theBuffer, 'F');
            if (searchPtr!=NULL) {
                SDUPSFeedrate=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindF;
                SERIAL_DEBUGLNPGM("found f");
            }
            
            if ((SDUPSState&(SDUPSStateFindX|SDUPSStateFindY|SDUPSStateFindZ|SDUPSStateFindE|SDUPSStateFindF|SDUPSStateFindCustom))==(SDUPSStateFindX|SDUPSStateFindY|SDUPSStateFindZ|SDUPSStateFindE|SDUPSStateFindF)) {
                break;
            }
        }
        else if (!strncmp_P(theBuffer, PSTR(";TYPE:"), strlen_P(PSTR(";TYPE:")))){
            if (!strncmp_P(theBuffer, PSTR(";TYPE:CUSTOM"), strlen_P(PSTR(";TYPE:CUSTOM")))){
                SDUPSState|=SDUPSStateFindCustom;
                SERIAL_DEBUGLNPGM("found Custom");
            }
            else{
                if ((SDUPSState&SDUPSStateFindCustom)==SDUPSStateFindCustom) {
                    SERIAL_DEBUGLNPGM("release Custom");
                    SDUPSState &= ~SDUPSStateFindCustom;
                    SDUPSState &= ~SDUPSStateFindE;
                }
            }
        }
    } while (true);
    return RESUME_ERROR_NONE;
}


static uint8_t doSearchPause(uint32_t& theFilePosition, char *theBuffer, uint8_t theSize)
{
    unsigned long printResumeTimer=millis();
    
    char* searchPtr;
    
    SDUPSState=SDUPSStateFindNone;
    card.setIndex(theFilePosition);
    
    do {
        
        if (millis()-printResumeTimer>=2000) {
            previous_millis_cmd = millis();
            return RESUME_ERROR_SD_SEARCH_TOO_LONG;
        }
        
        card.fgets(theBuffer, theSize);
        
        if (card.errorCode())
        {
            SERIAL_ECHOLNPGM("sd error");
            if (!card.sdInserted)
            {
                return RESUME_ERROR_SD_READ_CARD;
            }
            //On an error, reset the error, reset the file position and try again.
#ifdef ClearError
            card.clearError();
#endif
            card.setIndex(card.getFilePos());
            continue;
        }
        
        if (theBuffer[0]=='G') {
            
            searchPtr=strchr(theBuffer, 'X');
            if ((searchPtr!=NULL)&&((SDUPSState&SDUPSStateFindX)==0)) {
                SDUPSCurrentPosition[X_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindX;
                SERIAL_DEBUGLNPGM("found x");
            }
            
            searchPtr=strchr(theBuffer, 'Y');
            if (searchPtr!=NULL&&((SDUPSState&SDUPSStateFindY)==0)) {
                SDUPSCurrentPosition[Y_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindY;
                SERIAL_DEBUGLNPGM("found y");
            }
            
            searchPtr=strchr(theBuffer, 'E');
            if (searchPtr!=NULL&&((SDUPSState&SDUPSStateFindE)==0)) {
                SDUPSCurrentPosition[E_AXIS]=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindE;
                SERIAL_DEBUGLNPGM("found e");
            }
            
            searchPtr=strchr(theBuffer, 'F');
            if (searchPtr!=NULL&&((SDUPSState&SDUPSStateFindF)==0)) {
                SDUPSFeedrate=strtod(searchPtr+1, NULL);
                SDUPSState|=SDUPSStateFindF;
                SERIAL_DEBUGLNPGM("found f");
            }
            
            if ((SDUPSState&(SDUPSStateFindX|SDUPSStateFindY|SDUPSStateFindE|SDUPSStateFindF|SDUPSStateFindCustom))==(SDUPSStateFindX|SDUPSStateFindY|SDUPSStateFindE|SDUPSStateFindF)) {
                break;
            }
        }
        else if (!strncmp_P(theBuffer, PSTR(";TYPE:"), strlen_P(PSTR(";TYPE:")))){
            if (!strncmp_P(theBuffer, PSTR(";TYPE:CUSTOM"), strlen_P(PSTR(";TYPE:CUSTOM")))){
                SDUPSState|=SDUPSStateFindCustom;
                SERIAL_DEBUGLNPGM("found Custom");
            }
            else{
                if ((SDUPSState&SDUPSStateFindCustom)==SDUPSStateFindCustom) {
                    SERIAL_DEBUGLNPGM("release Custom");
                    SDUPSState &= ~SDUPSStateFindCustom;
                    SDUPSState &= ~SDUPSStateFindE;
                }
            }
        }
    } while (true);
    return RESUME_ERROR_NONE;
}

static void doManualPosition(char *theBuffer)
{
    char *bufferPtr;
    enquecommand_P(PSTR("G28\nG11\nG1 F6000 Z200"));
    
    bufferPtr=theBuffer;
    
    strcpy_P(bufferPtr, PSTR("G1 Z"));
    bufferPtr+=strlen_P(PSTR("G1 Z"));
    
    bufferPtr=float_to_string(min(SDUPSCurrentPosition[Z_AXIS]+20, 260), bufferPtr);
    
    strcpy_P(bufferPtr, PSTR("\nG1 X"));
    bufferPtr+=strlen_P(PSTR("\nG1 X"));
    
    bufferPtr=float_to_string(SDUPSCurrentPosition[X_AXIS], bufferPtr);
    
    strcpy_P(bufferPtr, PSTR(" Y"));
    bufferPtr+=strlen_P(PSTR(" Y"));
    
    bufferPtr=float_to_string(SDUPSCurrentPosition[Y_AXIS], bufferPtr);
    
    strcpy_P(bufferPtr, PSTR(" Z"));
    bufferPtr+=strlen_P(PSTR(" Z"));
    
    bufferPtr=float_to_string(SDUPSCurrentPosition[Z_AXIS]+1.0, bufferPtr);
    
    enquecommand(theBuffer);
}

static void doResumeHeatUp(uint32_t& theSDUPSFilePosition)
{
    card.setIndex(theSDUPSFilePosition);
    
    target_temperature_bed = 0;
    fanSpeedPercent = 0;
    for(uint8_t e=0; e<EXTRUDERS; e++)
    {
        if (LCD_DETAIL_CACHE_MATERIAL(e) < 1)
            continue;
        target_temperature[e] = 0;//material[e].temperature;
        target_temperature_bed = max(target_temperature_bed, material[e].bed_temperature);
        fanSpeedPercent = max(fanSpeedPercent, material[0].fan_speed);
        volume_to_filament_length[e] = 1.0 / (M_PI * (material[e].diameter / 2.0) * (material[e].diameter / 2.0));
        extrudemultiply[e] = material[e].flow;
    }
    
    fanSpeed = 0;
    lcd_change_to_menu(lcd_menu_print_heatup);
}

static void doResumeInit()
{
    char buffer[64];
  
    if (handleResumeError(doRetriveClass())) return;
  
    for (uint8_t index=1; index<SDUPSPositionSize; index++) {
        SDUPSFilePosition=SDUPSRetrievePosition(index);
        if (SDUPSFilePosition==0xffffffffUL) {
            if (handleResumeError(RESUME_ERROR_SD_READ_CARD)) return;
        }
        if (handleResumeError(doStableReadSD(SDUPSFilePosition, buffer, sizeof(buffer)))) return;
        if (!strncmp_P(buffer, PSTR(";LAYER:"), strlen_P(PSTR(";LAYER:")))) {
            if (index==1) {
                resumeState|=RESUME_STATE_RESUME;
                SERIAL_ECHOLN("RESUME_STATE_RESUME");
            }
            else{
                resumeState|=RESUME_STATE_PAUSE;
                SERIAL_ECHOLN("RESUME_STATE_PAUSE");
            }
            SERIAL_PROTOCOLLNPGM("RetriveIndex:");
            SERIAL_PROTOCOLLN((int)index);
            SERIAL_PROTOCOLLNPGM("Layer:");
            SERIAL_PROTOCOLLN(atoi(buffer+strlen_P(PSTR(";LAYER:"))));
            break;
        }
        if (index==SDUPSPositionSize-1) {
            if (handleResumeError(RESUME_ERROR_SD_READ_CARD)) return;
        }
    }
    
    if (handleResumeError(doSearchZLayer(SDUPSFilePosition, buffer, sizeof(buffer)))) return;
    
    if ((resumeState&RESUME_STATE_MANUAL)==RESUME_STATE_MANUAL) {
        doManualPosition(buffer);
    }
    else{
        if ((resumeState&RESUME_STATE_PAUSE)==RESUME_STATE_PAUSE) {
            SDUPSFilePosition=SDUPSRetrievePosition(1);
            if (SDUPSFilePosition==0xffffffffUL) {
                if (handleResumeError(RESUME_ERROR_SD_READ_CARD)) return;
            }
            doSearchPause(SDUPSFilePosition, buffer, sizeof(buffer));
        }
        doResumeHeatUp(SDUPSFilePosition);
    }
}

static void doResumeManualInit()
{
    resumeState|=RESUME_STATE_MANUAL;
    doResumeInit();
}

static void lcd_menu_print_resume_manual_option()
{
    LED_NORMAL();
    lcd_question_screen(NULL, doResumeInit, PSTR("CONTINUE"), lcd_menu_print_resume_manual_height, doResumeManualInit, PSTR("MANUAL"),MenuForward,MenuForward);
    lcd_lib_draw_string_centerP(10, PSTR("Continue to resume"));
    lcd_lib_draw_string_centerP(20, PSTR("or manually set the"));
    lcd_lib_draw_string_centerP(30, PSTR("height."));
}


/****************************************************************************************
 * resume manually set height
 *
 ****************************************************************************************/
static void doResumeManualStoreZ()
{
    
    SDUPSLastZ=current_position[Z_AXIS];
    
    enquecommand_P(PSTR("G28"));
    
    SDUPSPositionIndex=1;
}

static void lcd_menu_print_resume_manual_height()
{
    char buffer[32];
    char *bufferPtr;
    
    LED_NORMAL();
    
    if (printing_state == PRINT_STATE_NORMAL && lcd_lib_encoder_pos != 0 && movesplanned() < 4 && !is_command_queued() && !isCommandInBuffer() )
      {
        current_position[Z_AXIS] -= float(lcd_lib_encoder_pos) * 0.1;
        
        if (current_position[Z_AXIS]<0.0) {
            current_position[Z_AXIS]=0.0;
        }
        plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], 1500/60, 0);
      }
    lcd_lib_encoder_pos = 0;
    
    if (blocks_queued())
        lcd_info_screen(NULL, NULL, PSTR("CONTINUE"));
    else
        lcd_info_screen(lcd_menu_print_resume_manual_search_sd_card_eeprom, doResumeManualStoreZ, PSTR("CONTINUE"), MenuForward);
    
    bufferPtr=buffer;
    strcpy_P(bufferPtr, PSTR("Last Printed Z:"));
    bufferPtr+=strlen_P(PSTR("Last Printed Z:"));
    bufferPtr=float_to_string(SDUPSCurrentPosition[Z_AXIS], bufferPtr);
    lcd_lib_draw_string_center(10, buffer);
    
    lcd_lib_draw_string_centerP(20, PSTR("Press Down or UP"));
    
    bufferPtr=buffer;
    strcpy_P(bufferPtr, PSTR("Current Z:"));
    bufferPtr+=strlen_P(PSTR("Current Z:"));
    
    bufferPtr=float_to_string(current_position[Z_AXIS], bufferPtr);
    
    lcd_lib_draw_string_center(30, buffer);
}

static void lcd_menu_print_resume_manual_search_sd_card_eeprom()
{
    char buffer[64];
    static bool isFirst;

    lcd_info_screen(lcd_menu_print_abort,NULL,PSTR("ABORT"),MenuForward);
    
    lcd_lib_draw_string_centerP(20, PSTR("Searching SD card..."));
    
    lcd_progressbar(0);
    
    SDUPSFilePosition=SDUPSRetrievePosition(SDUPSPositionIndex);
    if (SDUPSFilePosition==0xffffffffUL) {
        SDUPSFilePosition=0;
        card.setIndex(SDUPSFilePosition);
        SDUPSCurrentPosition[Z_AXIS]=0;
        lcd_change_to_menu(lcd_menu_print_resume_manual_search_sd_card);
        return;
    }
    
    if (handleResumeError(doStableReadSD(SDUPSFilePosition, buffer, sizeof(buffer)))) return;

    if (SDUPSPositionIndex==1) {
        isFirst=true;
    }
    
    if (!strncmp_P(buffer, PSTR(";LAYER:"), strlen_P(PSTR(";LAYER:")))) {
        SERIAL_PROTOCOLLNPGM("Layer:");
        SERIAL_PROTOCOLLN(atoi(buffer+strlen_P(PSTR(";LAYER:"))));
        if(handleResumeError(doSearchZLayer(SDUPSFilePosition, buffer, sizeof(buffer)))) return;
        if (SDUPSCurrentPosition[Z_AXIS]<=SDUPSLastZ) {
            if (isFirst) {
                card.setIndex(SDUPSFilePosition);
                lcd_change_to_menu(lcd_menu_print_resume_manual_search_sd_card);
                SERIAL_DEBUGLNPGM("manual_search_sd_card");
                return;
            }
            else{
                doResumeHeatUp(SDUPSFilePosition);
                return;
            }
        }
        isFirst=false;
    }
    
    SDUPSPositionIndex++;
    
    if (SDUPSPositionIndex==SDUPSPositionSize) {
        SDUPSFilePosition=0;
        card.setIndex(SDUPSFilePosition);
        SDUPSCurrentPosition[Z_AXIS]=0;
        lcd_change_to_menu(lcd_menu_print_resume_manual_search_sd_card);
    }
}

/****************************************************************************************
 * resume manually Search SD card
 *
 ****************************************************************************************/
static void lcd_menu_print_resume_manual_search_sd_card()
{
    LED_NORMAL();
    lcd_info_screen(lcd_menu_print_abort,NULL,PSTR("ABORT"),MenuForward);
    
    lcd_lib_draw_string_centerP(20, PSTR("Searching SD card..."));
    
    lcd_progressbar((uint8_t)(125*SDUPSCurrentPosition[Z_AXIS]/SDUPSLastZ));
    
    if (printing_state!=PRINT_STATE_NORMAL || is_command_queued() || isCommandInBuffer()) {
        return;
    }
    
    
    char buffer[64];
    char *bufferPtr=NULL;
    
    unsigned long printResumeTimer=millis();
    
    do {
        if (millis()-printResumeTimer>=2000) {
            previous_millis_cmd = millis();
            return;
        }
        
        card.fgets(buffer, sizeof(buffer));
        
        if (card.errorCode())
          {
            SERIAL_ECHOLNPGM("sd error");
            if (!card.sdInserted)
              {
                return;
              }
            
#ifdef ClearError
            card.clearError();
#endif
            card.setIndex(card.getFilePos());
            
            return;
          }
        
        if (buffer[0]==';') {
            if (!strncmp_P(buffer, PSTR(";LAYER:"), 7)) {
                SERIAL_PROTOCOLLNPGM("Layer:");
                SERIAL_PROTOCOLLN(atoi(buffer+strlen_P(PSTR(";LAYER:"))));
                SDUPSFilePosition=card.getFilePos();
                if (handleResumeError(doSearchZLayer(SDUPSFilePosition, buffer, sizeof(buffer)))) return;
                if (SDUPSCurrentPosition[Z_AXIS]>=SDUPSLastZ) {
                    if (SDUPSCurrentPosition[Z_AXIS]>=SDUPSLastZ+5) {
                        SERIAL_DEBUGLNPGM("SDUPSCurrentPosition");
                        SERIAL_DEBUGLN(SDUPSCurrentPosition[Z_AXIS]);
                        SERIAL_DEBUGLNPGM("SDUPSLastZ");
                        SERIAL_DEBUGLN(SDUPSLastZ);
                        
                        if (handleResumeError(RESUME_ERROR_Z_RANGE)) return;
                    }
                    doResumeHeatUp(SDUPSFilePosition);
                }
            }
        }
        else if (buffer[0]=='M'){
            if(!strncmp_P(buffer, PSTR("M25"), 3)){
                if (handleResumeError(RESUME_ERROR_M25)) return;
            }
        }
    } while (true);
    
    previous_millis_cmd = millis();
}

/****************************************************************************************
 * resume error
 *
 ****************************************************************************************/
static void doClearResumeError()
{
    resumeError=RESUME_ERROR_NONE;
    if (card.isFileOpen()) {
        card.closefile();
    }
    card.release();
}

static void lcd_menu_print_resume_error()
{
    LED_GLOW();
    lcd_info_screen(lcd_menu_main,doClearResumeError,PSTR("CANCEL"),MenuForward);
    
    char buffer[32];
    
    if (resumeError) {
        switch (resumeError) {
            case RESUME_ERROR_Z_RANGE:
                lcd_lib_draw_string_centerP(20, PSTR("Z doesn't match"));
                break;
            case RESUME_ERROR_M25:
                lcd_lib_draw_string_centerP(20, PSTR("M25 found"));
                break;
            case RESUME_ERROR_SDUPSState:
                lcd_lib_draw_string_centerP(20, PSTR("SDUPSState wrong"));
                int_to_string(SDUPSState, buffer);
                lcd_lib_draw_string_center(30, buffer);
                break;
            case RESUME_ERROR_SD_VALIDATE:
                lcd_lib_draw_string_centerP(20, PSTR("SDUPS Validate wrong"));
                break;
            case RESUME_ERROR_SD_FILEPOSITION:
                lcd_lib_draw_string_centerP(20, PSTR("EEPROM file position"));
                int_to_string(SDUPSFilePosition, buffer);
                lcd_lib_draw_string_center(30, buffer);
                break;
            case RESUME_ERROR_SD_READ_CARD:
                lcd_lib_draw_string_centerP(20, PSTR("SD read error"));
                break;
            case RESUME_ERROR_SD_SEARCH_TOO_LONG:
                lcd_lib_draw_string_centerP(20, PSTR("SD search too long"));
                break;
                
            default:
                break;
        }
    }


}


/****************************************************************************************
 * Check whether the print is finished
 *
 ****************************************************************************************/
static void checkPrintFinished()
{
    if (!card.sdprinting && !is_command_queued() && !isCommandInBuffer())
      {
#ifdef SDUPS
        SDUPSDone();
#endif
        abortPrint();
        currentMenu = lcd_menu_print_ready;
        SELECT_MAIN_MENU_ITEM(0);
      }
    if (card.errorCode())
      {
        abortPrint();
        currentMenu = lcd_menu_print_error;
        SELECT_MAIN_MENU_ITEM(0);
      }
    
    
#ifdef GATE_PRINT
    
    static bool gateState = false;
    static uint8_t gateStateDelay = 0;
    bool newGateState = READ(GATE_PIN);
    
    if (gateState != newGateState)
    {
        if (gateStateDelay){
            gateStateDelay--;
        }
        else{
            gateState = newGateState;
            if (newGateState) {
                doPausePrint();
                lcd_change_to_menu(lcd_menu_print_pausing);
            }
        }
    }else{
        gateStateDelay = 10;
    }
    
#endif
    
    
#ifdef FilamentDetection
    
    if (isFilamentDetectionEnable) {
        static unsigned long FilamentDetectionTimer=millis();
        if (READ(FilamentDetectionPin)) {
            if (millis()-FilamentDetectionTimer>500) {
                abortPrint();
                currentMenu = lcd_menu_print_out_of_filament;
                SELECT_MAIN_MENU_ITEM(0);
            }
        }
        else{
            FilamentDetectionTimer=millis();
        }
    }
    
#endif
}

/****************************************************************************************
 * Check the filament
 *
 ****************************************************************************************/
#ifdef FilamentDetection
static void doOutOfFilament()
{
  resumeState=RESUME_STATE_FILAMENT;
}

static void lcd_menu_print_out_of_filament()
{
  LED_GLOW();
  lcd_question_screen(lcd_menu_change_material_preheat, doOutOfFilament, PSTR("CONTINUE"), lcd_menu_main, NULL, PSTR("CANCEL"));
  lcd_lib_draw_string_centerP(10, PSTR("OverLord is out"));
  lcd_lib_draw_string_centerP(20, PSTR("of material. Press"));
  lcd_lib_draw_string_centerP(30, PSTR("CONTINUE to change"));
  lcd_lib_draw_string_centerP(40, PSTR("the material."));
}
#endif


#endif//ENABLE_ULTILCD2
