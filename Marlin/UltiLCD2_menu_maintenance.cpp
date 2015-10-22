#include "Configuration.h"
#ifdef ENABLE_ULTILCD2
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "UltiLCD2_gfx.h"
#include "UltiLCD2_menu_maintenance.h"
#include "UltiLCD2_menu_first_run.h"
#include "UltiLCD2_menu_material.h"
#include "cardreader.h"
#include "lifetime_stats.h"
#include "ConfigurationStore.h"
#include "temperature.h"
#include "pins.h"
#include "UltiLCD2_menu_maintenance.h"
#include "fitting_bed.h"

static void lcd_menu_maintenance_retraction();
static void lcd_retraction_details(uint8_t nr);
static char* lcd_retraction_item(uint8_t nr);
static void lcd_menu_advanced_factory_reset();
static void doFactoryReset();
static void lcd_menu_advanced_state();
void lcd_menu_advanced_version();
static void lcd_menu_advanced_level();

static void doAdvancedLevelDone();
static void doAdvancedLevel();
void lcd_menu_maintenance_advanced_bed_heatup();
static void lcd_menu_maintenance_extrude();
static void lcd_menu_maintenance_advanced_heatup();
static void lcd_menu_advanced_about();
static void lcd_advanced_about_details(uint8_t nr);
static char* lcd_advanced_about_item(uint8_t nr);
static void lcd_advanced_settings_details(uint8_t nr);
static char* lcd_advanced_settings_item(uint8_t nr);
static void lcd_menu_advanced_movement();
static void lcd_advanced_movement_details(uint8_t nr);
static char* lcd_advanced_movement_item(uint8_t nr);
static void lcd_menu_advanced_temperature();
static void lcd_advanced_temperature_details(uint8_t nr);
static char* lcd_advanced_temperature_item(uint8_t nr);
static void lcd_menu_maintenance_advanced();
static void lcd_advanced_details(uint8_t nr);
static char* lcd_advanced_item(uint8_t nr);
static void lcd_menu_maintenance_details(uint8_t nr);
static char* lcd_menu_maintenance_item(uint8_t nr);
static void lcd_menu_maintenance_waiting_auto_level();

static void lcd_menu_maintenance_select_level();
static void lcd_menu_maintenance_select_manual_level();
static void lcd_menu_maintenance_heat_for_level();
static void lcd_menu_maintenance_heat_for_manual_level();
static void lcd_menu_maintenance_manual_level();

#define CIRCLE_RADIUS 75.0
#define CIRCLE_RADIUS_STRING "75"


static float circleDegree=0;
static float circleRadius=CIRCLE_RADIUS;
static float maintenanceLevelZ=ADDING_Z_FOR_POSITIVE;

#define MANUAL_LEVEL_NONE 0
#define MANUAL_LEVEL_ENABLE 1
#define MANUAL_LEVLE_UPDATE 2
uint8_t manualLevelState=MANUAL_LEVEL_NONE;

/****************************************************************************************
 * Maintenance Menu
 *
 ****************************************************************************************/
static char* lcd_menu_maintenance_item(uint8_t nr)
{
    if (nr == 0)
        strcpy_P(card.longFilename, PSTR("\x82""Reload"));
    else if (nr == 1)
        strcpy_P(card.longFilename, PSTR("\x83""Calibrate"));
    else
        strcpy_P(card.longFilename, PSTR("???"));
    return card.longFilename;
}

static void lcd_menu_maintenance_details(uint8_t nr)
{
    char buffer[22];
    if (nr == 0)
    {
#ifdef FilamentDetection
      if (isFilamentDetectionEnable) {
        if (FilamentAvailable()) {
          lcd_draw_detailP(PSTR("Material Yes."));
        }
        else{
          lcd_draw_detailP(PSTR("Material No."));
        }
      }
      else{
        lcd_draw_detailP(PSTR("Change the Material."));
      }
#else
    lcd_draw_detailP(PSTR("Change the Material."));
#endif
    }
    else if (nr == 1)
        lcd_draw_detailP(PSTR("Level the Platform."));
    else
        lcd_draw_detailP(PSTR("???"));

}

void lcd_menu_maintenance()
{
    
    LED_NORMAL();
    
    lcd_normal_menu(PSTR("Maintenance Menu"), 2, lcd_menu_maintenance_item, lcd_menu_maintenance_details);
    lcd_lib_draw_gfx(0, 0, maintenanceGfx);

    if (IS_SELECTED_SCROLL(-1)) {
        lcd_change_to_menu(lcd_menu_main, SCROLL_MENU_ITEM_POS(0), MenuUp);
    }
    if (IS_SELECTED_SCROLL(2)) {
        lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(0), MenuDown);
    }
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_SCROLL(0))
            lcd_change_to_menu(lcd_menu_change_material_preheat, SCROLL_MENU_ITEM_POS(0), MenuForward);
        else if (IS_SELECTED_SCROLL(1))
            lcd_change_to_menu(lcd_menu_maintenance_select_level, SCROLL_MENU_ITEM_POS(0), MenuForward);
    }
    
}

    /****************************************************************************************
     * Level select Menu
     *
     ****************************************************************************************/


static void lcd_menu_maintenance_select_level()
{
    lcd_question_screen(lcd_menu_maintenance_heat_for_level, NULL, PSTR("YES"), lcd_menu_maintenance_select_manual_level, NULL, PSTR("NO"), MenuForward, MenuForward);
    
    lcd_lib_draw_string_centerP(10, PSTR("Would you like to"));
    lcd_lib_draw_string_centerP(20, PSTR("Automatically level"));
    lcd_lib_draw_string_centerP(30, PSTR("the buildplate?"));
}

    /****************************************************************************************
     * Level select Manual Menu
     *
     ****************************************************************************************/

static void doManualLevelHeat()
{
    target_temperature_bed = 0;
    fanSpeedPercent = 0;
    for(uint8_t e=0; e<EXTRUDERS; e++)
    {
        target_temperature[e] = material[e].temperature;
        fanSpeedPercent = max(fanSpeedPercent, material[0].fan_speed);
        volume_to_filament_length[e] = 1.0 / (M_PI * (material[e].diameter / 2.0) * (material[e].diameter / 2.0));
        extrudemultiply[e] = material[e].flow;
    }
    
    target_temperature_bed = 0;
    fanSpeed = 0;
    SERIAL_ECHOLNPGM("target_temperature_bed");
    SERIAL_ECHOLN(target_temperature_bed);
}

static void doCancelManualLevel()
{
    nextEncoderPos=1;
}

static void lcd_menu_maintenance_select_manual_level()
{
    lcd_question_screen(lcd_menu_maintenance_heat_for_manual_level, doManualLevelHeat, PSTR("YES"), lcd_menu_maintenance, doCancelManualLevel, PSTR("NO"));
    
    lcd_lib_draw_string_centerP(10, PSTR("Would you like to"));
    lcd_lib_draw_string_centerP(20, PSTR("Manually level"));
    lcd_lib_draw_string_centerP(30, PSTR("the buildplate?"));
}

        /****************************************************************************************
         * Auto level :waiting heating
         *
         ****************************************************************************************/
static void doMaintenanceCancelAutoLevel()
{
    lcd_change_to_menu(lcd_menu_maintenance,1,MenuBackward);
    setTargetHotend(0, 0);
}

static void lcd_menu_maintenance_heat_for_level()
{
    LED_GLOW_HEAT();
    setTargetHotend(160, 0);
    int16_t temp = degHotend(0) - 20;
    int16_t target = degTargetHotend(0) - 10 - 20;
    if (temp < 0) temp = 0;
    if (temp > target){
        setTargetHotend(0, 0);
        setTargetBed(0);
        enquecommand_P(PSTR("G29\nM84"));
        currentMenu = lcd_menu_maintenance_waiting_auto_level;
        temp = target;
    }
    
    uint8_t progress = uint8_t(temp * 125 / target);
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;
    
    lcd_info_screen(NULL, doMaintenanceCancelAutoLevel, PSTR("CANCEL"), MenuForward);
    lcd_lib_draw_string_centerP(10, PSTR("Printhead heating in"));
    lcd_lib_draw_string_centerP(20, PSTR("case that material"));
    lcd_lib_draw_string_centerP(30, PSTR("is left around it."));
    
    lcd_progressbar(progress);
}


        /****************************************************************************************
         * Auto level :waiting Auto leveling finishing
         *
         ****************************************************************************************/
static void doMaintenanceCancelAutoLevelProcession()
{
    doMaintenanceCancelAutoLevel();
    printing_state=PRINT_STATE_HOMING_ABORT;
}

static void lcd_menu_maintenance_waiting_auto_level()
{
    LED_NORMAL();
    if (printing_state!=PRINT_STATE_NORMAL || is_command_queued() || isCommandInBuffer()) {
        lcd_info_screen(NULL, doMaintenanceCancelAutoLevelProcession, PSTR("ABORT"), MenuForward);
    }
    else{
        lcd_change_to_menu(lcd_menu_maintenance,1,MenuBackward);
    }
    
    lcd_lib_draw_string_centerP(10, PSTR("The Nozzle will"));
    lcd_lib_draw_string_centerP(20, PSTR("touch the buildplate"));
    lcd_lib_draw_string_centerP(30, PSTR("gently to do the"));
    lcd_lib_draw_string_centerP(40, PSTR("calibration process."));
}


        /****************************************************************************************
         * Manual Level :heat_for_manual_level
         ****************************************************************************************/
static void doMaintenanceLevel()
{
    char buffer[64];
    char *bufferPtr;

    circleDegree=0;
    circleRadius=CIRCLE_RADIUS;
    manualLevelState=MANUAL_LEVEL_ENABLE;
    fittingBedOffsetInit();
    
    fittingBedArrayInit();
    plainFactorA=plainFactorABackUp;
    plainFactorB=plainFactorBBackUp;
    plainFactorC=plainFactorCBackUp;
    
    for (uint8_t index=0; index<NodeNum; index++) {
        fittingBedArray[index][Z_AXIS]=(-1.0-plainFactorA*fittingBedArray[index][X_AXIS]-plainFactorB*fittingBedArray[index][Y_AXIS])/plainFactorC;
        fittingBedArray[index][Z_AXIS]+=fittingBedOffset[index];
    }
  
    fittingBedRaw();
  
    add_homeing[Z_AXIS] = 1.0/plainFactorC;
    add_homeing[Z_AXIS] -= -ADDING_Z_FOR_POSITIVE;
    add_homeing[Z_AXIS] -= touchPlateOffset;
  
    maintenanceLevelZ=-1.0/plainFactorC;

    SERIAL_DEBUGLNPGM("add_homeing[Z_AXIS]");
    SERIAL_DEBUGLN(add_homeing[Z_AXIS]);
  
    enquecommand_P(PSTR("G28"));
    enquecommand_P(PSTR("G1 F6000 Z50\nG1 X0 Y"Y_MIN_POS_STR" Z" PRIMING_HEIGHT_STRING));
    
    for(uint8_t e = 0; e<EXTRUDERS; e++)
    {
        
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
    
    enquecommand_P(PSTR("G1 X"CIRCLE_RADIUS_STRING" Y0 Z0.3 F3000"));
}

static void doCancelHeatManualLevel()
{
    nextEncoderPos=1;
    abortPrint();
}

static void lcd_menu_maintenance_heat_for_manual_level()
{
    LED_GLOW_HEAT();
    
    static unsigned long heatupTimer=millis();
    
    lcd_info_screen(lcd_menu_maintenance, doCancelHeatManualLevel, PSTR("ABORT"), MenuForward);

    for(uint8_t e=0; e<EXTRUDERS; e++)
      {
        if (current_temperature[e] < target_temperature[e] - TEMP_HYSTERESIS || current_temperature[e] > target_temperature[e] + TEMP_HYSTERESIS) {
            heatupTimer=millis();
        }
      }
    
    if (millis()-heatupTimer>=TEMP_RESIDENCY_TIME*1000UL && printing_state == PRINT_STATE_NORMAL)
      {
        doMaintenanceLevel();
        lcd_change_to_menu(lcd_menu_maintenance_manual_level);
      }
    
    uint8_t progress = 125;
    for(uint8_t e=0; e<EXTRUDERS; e++)
      {
          if (current_temperature[e] > 20)
              progress = min(progress, (current_temperature[e] - 20) * 125 / (target_temperature[e] - 20 - TEMP_WINDOW));
          else
              progress = 0;
      }
    
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;
    
    lcd_lib_draw_string_centerP(10, PSTR("Heating up..."));
    lcd_lib_draw_string_centerP(20, PSTR("Preparing for"));
    lcd_lib_draw_string_centerP(30, PSTR("Calibration"));
  
    lcd_progressbar(progress);
}


        /****************************************************************************************
         * Manual Level :Manual_level
         ****************************************************************************************/
static void doFinishManualLevel()
{
    manualLevelState=MANUAL_LEVEL_NONE;
  
    add_homeing[Z_AXIS] = 1.0/plainFactorC;
    add_homeing[Z_AXIS] -= -ADDING_Z_FOR_POSITIVE;
    add_homeing[Z_AXIS] -= touchPlateOffset;
    SERIAL_DEBUGLNPGM("add_homeing[Z_AXIS]");
    SERIAL_DEBUGLN(add_homeing[Z_AXIS]);
    Config_StoreSettings();
  
    doCooldown();
    enquecommand_P(PSTR("G28\nM84"));
    nextEncoderPos=1;
}


void manualLevelRoutine()
{
    if (manualLevelState) {
        
        if (commands_queued() || is_command_queued()) {
            return;
        }
        
        static unsigned long manualLevelTimer=millis();
        
        if (feedrate>=1000) {
            manualLevelTimer=millis();
        }
        else{
            if (millis()-manualLevelTimer>1000) {
                feedrate=3000;
            }
        }
        
        
        while (movesplanned()<=3) {
            circleDegree+=1/(circleRadius*2*PI);
            
            if (circleDegree>=2*PI) {
                circleDegree-=2*PI;
                circleRadius-=0.4;
                plan_set_e_position(0);
                if (circleRadius<0) {
                    return;
                }
            }
            
            current_position[X_AXIS]=circleRadius*cos(circleDegree);
            current_position[Y_AXIS]=circleRadius*sin(circleDegree);
            current_position[E_AXIS]=circleRadius*circleDegree/10;
            
            //x y small move
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], feedrate/60, active_extruder);
        }
        
    }
}


static void lcd_menu_maintenance_manual_level()
{
    if (printing_state == PRINT_STATE_NORMAL && !commands_queued() && !is_command_queued()) {\
        
        float circleDegreeBuffer;
        
        if (lcd_lib_encoder_pos != 0) {
            
            feedrate=500;
            
            circleDegreeBuffer=circleDegree-(movesplanned()/(circleRadius*2*PI));
            
            fittingBedArray[0][X_AXIS]=circleRadius*cos(circleDegreeBuffer);
            
            SERIAL_ECHOLNPGM("fittingBedArray");
            
            for (uint8_t index=0; index<NodeNum; index++) {
                fittingBedArray[index][X_AXIS]=circleRadius*cos(circleDegreeBuffer);
                fittingBedArray[index][Y_AXIS]=circleRadius*sin(circleDegreeBuffer);
                circleDegreeBuffer+=2*PI/NodeNum;
                fittingBedArray[index][Z_AXIS]=(-1.0-plainFactorA*fittingBedArray[index][X_AXIS]-plainFactorB*fittingBedArray[index][Y_AXIS])/plainFactorC;
                
                //            SERIAL_ECHOLN(int(degreeIndex));
//                SERIAL_ECHOLN(fittingBedArray[index][Z_AXIS]);
            }
            
            if (lcd_lib_encoder_pos>0) {
                fittingBedArray[0][Z_AXIS] -= 0.05;
                fittingBedArray[1][Z_AXIS] -= 0.0375;
                fittingBedArray[2][Z_AXIS] -= 0.0125;
                fittingBedArray[3][Z_AXIS] -= 0;
                fittingBedArray[4][Z_AXIS] -= 0.0125;
                fittingBedArray[5][Z_AXIS] -= 0.0375;
//                fittingBedArray[6][Z_AXIS] -= 0.05;
//                fittingBedArray[7][Z_AXIS] -= 0.085355339;
            }
            else{
                fittingBedArray[0][Z_AXIS] += 0.05;
                fittingBedArray[1][Z_AXIS] += 0.0375;
                fittingBedArray[2][Z_AXIS] += 0.0125;
                fittingBedArray[3][Z_AXIS] += 0;
                fittingBedArray[4][Z_AXIS] += 0.0125;
                fittingBedArray[5][Z_AXIS] += 0.0375;
//                fittingBedArray[6][Z_AXIS] += 0.05;
//                fittingBedArray[7][Z_AXIS] += 0.085355339;
            }
            
            if (fittingBedArray[0][Z_AXIS]<0.5) {
                for (uint8_t index=0; index<NodeNum; index++) {
                    fittingBedArray[index][Z_AXIS]=0.5;
                }
            }
          
            SERIAL_DEBUGLNPGM("plainFactor old:");
            SERIAL_DEBUGLN(plainFactorA*1000000.0);
            SERIAL_DEBUGLN(plainFactorB*1000000.0);
            SERIAL_DEBUGLN(plainFactorC*1000000.0);
            
            fittingBedRaw();
            fittingBedUpdateK();
            
            SERIAL_DEBUGLNPGM("plainFactor new:");
            SERIAL_DEBUGLN(plainFactorA*1000000.0);
            SERIAL_DEBUGLN(plainFactorB*1000000.0);
            SERIAL_DEBUGLN(plainFactorC*1000000.0);
            
            fittingBedArrayInit();
            
            for (uint8_t index=0; index<NodeNum; index++) {
                fittingBedOffset[index]=(-1.0-plainFactorA*fittingBedArray[index][X_AXIS]-plainFactorB*fittingBedArray[index][Y_AXIS])/plainFactorC-(-1.0-plainFactorABackUp*fittingBedArray[index][X_AXIS]-plainFactorBBackUp*fittingBedArray[index][Y_AXIS])/plainFactorCBackUp;
            }
            
            current_position[Z_AXIS]=-1.0/plainFactorC-maintenanceLevelZ+0.3;
            
            lcd_lib_encoder_pos=0;
        }
    }
    
    
    lcd_info_screen(lcd_menu_maintenance, doFinishManualLevel, PSTR("DONE"), MenuBackward);
    lcd_lib_draw_string_centerP(10, PSTR("Press Down or UP"));
    lcd_lib_draw_string_centerP(20, PSTR("to move the hotend"));
    lcd_lib_draw_string_centerP(30, PSTR("until the print"));
    lcd_lib_draw_string_centerP(40, PSTR("works well."));
}

/****************************************************************************************
 * Advanced Menu
 *
 ****************************************************************************************/

static char* lcd_advanced_item(uint8_t nr)
{
    if (nr == 0)
        strcpy_P(card.longFilename, PSTR("\x82""Temperature"));
    else if (nr == 1)
        strcpy_P(card.longFilename, PSTR("\x80""Movement"));
    else if (nr == 2)
        strcpy_P(card.longFilename, PSTR("\x80""Settings"));
    else if (nr == 3)
        strcpy_P(card.longFilename, PSTR("\x80""About"));
    else
        strcpy_P(card.longFilename, PSTR("???"));
    return card.longFilename;
}

static void lcd_advanced_details(uint8_t nr)
{
    char buffer[22];
    if (nr == 0)
        lcd_draw_detailP(PSTR("Change the Temperature."));
    else if (nr == 1)
        lcd_draw_detailP(PSTR("Movement."));
    else if (nr == 2)
        lcd_draw_detailP(PSTR("Settings."));
    else if (nr == 3)
        lcd_draw_detailP(PSTR("About."));
    else
        lcd_draw_detailP(PSTR("???"));
}

static void lcd_menu_maintenance_advanced()
{
    LED_NORMAL();
    
    if (lcd_lib_encoder_pos >= 3 * ENCODER_TICKS_PER_SCROLL_MENU_ITEM)
        lcd_lib_encoder_pos = 3 * ENCODER_TICKS_PER_SCROLL_MENU_ITEM;
    
    lcd_normal_menu(PSTR("Maintenance Menu"), 4, lcd_advanced_item, lcd_advanced_details);
    lcd_lib_draw_gfx(0, 0, advancedGfx);
    
    
    if (IS_SELECTED_SCROLL(-1)) {
        lcd_change_to_menu(lcd_menu_maintenance, SCROLL_MENU_ITEM_POS(1), MenuUp);
    }
    
    if (lcd_lib_button_pressed)
      {
        if (IS_SELECTED_SCROLL(0))
            lcd_change_to_menu(lcd_menu_advanced_temperature, SCROLL_MENU_ITEM_POS(1), MenuForward);
        else if (IS_SELECTED_SCROLL(1))
          {
            lcd_change_to_menu(lcd_menu_advanced_movement, SCROLL_MENU_ITEM_POS(1), MenuForward);
          }
        else if (IS_SELECTED_SCROLL(2))
          {
            lcd_change_to_menu(lcd_menu_advanced_settings, SCROLL_MENU_ITEM_POS(1), MenuForward);
          }
        else if (IS_SELECTED_SCROLL(3))
          {
            lcd_change_to_menu(lcd_menu_advanced_about, SCROLL_MENU_ITEM_POS(1), MenuForward);
          }
      }
}

    /****************************************************************************************
     * Temperature Menu
     *
     ****************************************************************************************/
static char* lcd_advanced_temperature_item(uint8_t nr)
{
#if TEMP_SENSOR_BED != 0
  if (nr == 0)
      strcpy_P(card.longFilename, PSTR("Return"));
  else if (nr == 1)
      strcpy_P(card.longFilename, PSTR("Heatup nozzle"));
  else if (nr == 2)
      strcpy_P(card.longFilename, PSTR("Heatup buildplate"));
  else if (nr == 3)
      strcpy_P(card.longFilename, PSTR("Fan speed"));
  else
      strcpy_P(card.longFilename, PSTR("???"));
  return card.longFilename;
#else
  if (nr == 0)
    strcpy_P(card.longFilename, PSTR("Return"));
  else if (nr == 1)
    strcpy_P(card.longFilename, PSTR("Heatup nozzle"));
  else if (nr == 2)
    strcpy_P(card.longFilename, PSTR("Fan speed"));
  else
    strcpy_P(card.longFilename, PSTR("???"));
  return card.longFilename;
#endif
}

static void lcd_advanced_temperature_details(uint8_t nr)
{
#if TEMP_SENSOR_BED != 0
  char buffer[22];
  
  char* c = (char*)buffer;
  
  if (nr == 0)
      lcd_draw_detailP(PSTR("Return to Advanced."));
  else if (nr == 1)
    {
      c = int_to_string(current_temperature[0], c, PSTR("\x81""C"));
      *c++ = '/';
      c = int_to_string(target_temperature[0], c, PSTR("\x81""C"));
      
      lcd_draw_detail((char*)buffer);
    }
  else if (nr == 2)
    {
      c = int_to_string(current_temperature_bed, c, PSTR("\x81""C"));
      *c++ = '/';
      c = int_to_string(target_temperature_bed, c, PSTR("\x81""C"));
      
      lcd_draw_detail((char*)buffer);
    }
  else if (nr == 3)
    {
      c = int_to_string(lround((int(fanSpeed) * 100) / 255.0), c, PSTR("%"));
      
      lcd_draw_detail((char*)buffer);
    }
  else
      lcd_draw_detailP(PSTR("???"));
#else
  char buffer[22];
  
  char* c = (char*)buffer;
  
  if (nr == 0)
    lcd_draw_detailP(PSTR("Return to Advanced."));
  else if (nr == 1)
  {
    c = int_to_string(current_temperature[0], c, PSTR("\x81""C"));
    *c++ = '/';
    c = int_to_string(target_temperature[0], c, PSTR("\x81""C"));
    
    lcd_draw_detail((char*)buffer);
  }
  else if (nr == 2)
  {
    c = int_to_string(lround((int(fanSpeed) * 100) / 255.0), c, PSTR("%"));
    
    lcd_draw_detail((char*)buffer);
  }
  else
    lcd_draw_detailP(PSTR("???"));
#endif
}

static void lcd_menu_advanced_temperature()
{
#if TEMP_SENSOR_BED != 0

  LED_NORMAL();
  if (lcd_lib_encoder_pos < 0) lcd_lib_encoder_pos = 0;
  
  lcd_advance_menu(PSTR("Tempature"), 4, lcd_advanced_temperature_item, lcd_advanced_temperature_details);
  
  if (IS_SELECTED_SCROLL(4)) {
      lcd_change_to_menu(lcd_menu_advanced_movement, SCROLL_MENU_ITEM_POS(1), MenuDown);
  }
  
  if (lcd_lib_button_pressed)
    {
      if (IS_SELECTED_SCROLL(0))
          lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(0), MenuBackward);
      else if (IS_SELECTED_SCROLL(1))
        {
          active_extruder = 0;
          lcd_change_to_menu(lcd_menu_maintenance_advanced_heatup, 0);
          lcd_lib_button_up_down_reversed = true;
        }
      else if (IS_SELECTED_SCROLL(2))
        {
          lcd_change_to_menu(lcd_menu_maintenance_advanced_bed_heatup, 0);
          lcd_lib_button_up_down_reversed = true;
        }
      else if (IS_SELECTED_SCROLL(3))
        {
          LCD_EDIT_SETTING_BYTE_PERCENT(fanSpeed, "Fan speed", "%", 0, 100);
        }
    }
#else
  LED_NORMAL();
  if (lcd_lib_encoder_pos < 0) lcd_lib_encoder_pos = 0;
  
  lcd_advance_menu(PSTR("Tempature"), 3, lcd_advanced_temperature_item, lcd_advanced_temperature_details);
  
  if (IS_SELECTED_SCROLL(3)) {
    lcd_change_to_menu(lcd_menu_advanced_movement, SCROLL_MENU_ITEM_POS(1), MenuDown);
  }
  
  if (lcd_lib_button_pressed)
  {
    if (IS_SELECTED_SCROLL(0))
      lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(0), MenuBackward);
    else if (IS_SELECTED_SCROLL(1))
    {
      active_extruder = 0;
      lcd_change_to_menu(lcd_menu_maintenance_advanced_heatup, 0);
      lcd_lib_button_up_down_reversed = true;
    }
    else if (IS_SELECTED_SCROLL(2))
    {
      LCD_EDIT_SETTING_BYTE_PERCENT(fanSpeed, "Fan speed", "%", 0, 100);
    }
  }
#endif
}

        /****************************************************************************************
         *Hotend Heatup
         ****************************************************************************************/
static void lcd_menu_maintenance_advanced_heatup()
{
    LED_NORMAL();
    if (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM != 0)
      {
        target_temperature[active_extruder] = int(target_temperature[active_extruder]) + (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM);
        if (target_temperature[active_extruder] < 0)
            target_temperature[active_extruder] = 0;
        if (target_temperature[active_extruder] > HEATER_0_MAXTEMP - 15)
            target_temperature[active_extruder] = HEATER_0_MAXTEMP - 15;
        lcd_lib_encoder_pos = 0;
      }
    if (lcd_lib_button_pressed)
      {
        lcd_lib_button_up_down_reversed = false;
        lcd_change_to_menu(previousMenu, previousEncoderPos, MenuBackward);
      }
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(20, PSTR("Nozzle temperature:"));
    lcd_lib_draw_string_centerP(57, PSTR("Click to return"));
    char buffer[16];
    int_to_string(int(current_temperature[active_extruder]), buffer, PSTR("\x81""C/"));
    int_to_string(int(target_temperature[active_extruder]), buffer+strlen(buffer), PSTR("\x81""C"));
    lcd_lib_draw_string_center(30, buffer);
}

        /****************************************************************************************
         *bed Heatup
         ****************************************************************************************/
#if TEMP_SENSOR_BED != 0
void lcd_menu_maintenance_advanced_bed_heatup()
{
    LED_NORMAL();
    if (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM != 0)
      {
        target_temperature_bed = int(target_temperature_bed) + (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM);
        if (target_temperature_bed < 0)
            target_temperature_bed = 0;
        if (target_temperature_bed > BED_MAXTEMP - 15)
            target_temperature_bed = BED_MAXTEMP - 15;
        lcd_lib_encoder_pos = 0;
      }
    if (lcd_lib_button_pressed)
      {
        lcd_change_to_menu(previousMenu, previousEncoderPos, MenuBackward);
        lcd_lib_button_up_down_reversed = false;
      }
    
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(20, PSTR("Buildplate temp.:"));
    lcd_lib_draw_string_centerP(57, PSTR("Click to return"));
    char buffer[16];
    int_to_string(int(current_temperature_bed), buffer, PSTR("\x81""C/"));
    int_to_string(int(target_temperature_bed), buffer+strlen(buffer), PSTR("\x81""C"));
    lcd_lib_draw_string_center(30, buffer);
}
#endif
    /****************************************************************************************
     * Movement Menu
     *
     ****************************************************************************************/
static char* lcd_advanced_movement_item(uint8_t nr)
{
  if (nr == 0)
      strcpy_P(card.longFilename, PSTR("Return"));
  else if (nr == 1)
      strcpy_P(card.longFilename, PSTR("Home Head"));
  else if (nr == 2)
      strcpy_P(card.longFilename, PSTR("Move Material"));
  else
      strcpy_P(card.longFilename, PSTR("???"));
  return card.longFilename;
}

static void lcd_advanced_movement_details(uint8_t nr)
{
  if (nr == 0)
      lcd_draw_detailP(PSTR("Return to Advanced."));
  else if (nr == 1)
    {
      lcd_draw_detailP(PSTR("Home the Head."));
    }
  else if (nr == 2)
    {
      lcd_draw_detailP(PSTR("Move the Material."));
    }
  else
      lcd_draw_detailP(PSTR("???"));
}

static void lcd_menu_advanced_movement()
{
  LED_NORMAL();
  lcd_advance_menu(PSTR("Movement"), 3, lcd_advanced_movement_item, lcd_advanced_movement_details);
  
  if (IS_SELECTED_SCROLL(-1)) {
#if TEMP_SENSOR_BED != 0
    lcd_change_to_menu(lcd_menu_advanced_temperature, SCROLL_MENU_ITEM_POS(3), MenuUp);
#else
    lcd_change_to_menu(lcd_menu_advanced_temperature, SCROLL_MENU_ITEM_POS(2), MenuUp);
#endif
  }
  
  if (IS_SELECTED_SCROLL(3)) {
      lcd_change_to_menu(lcd_menu_advanced_settings, SCROLL_MENU_ITEM_POS(1), MenuDown);
  }
  
  if (lcd_lib_button_pressed)
    {
      if (IS_SELECTED_SCROLL(0))
          lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(1), MenuBackward);
      else if (IS_SELECTED_SCROLL(1))
        {
          lcd_lib_beep();
          enquecommand_P(PSTR("G28\nM84"));
        }
      else if (IS_SELECTED_SCROLL(2))
        {
          //            set_extrude_min_temp(0);
          active_extruder = 0;
          target_temperature[active_extruder] = material[active_extruder].temperature;
          plan_set_position(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS]);
          lcd_change_to_menu(lcd_menu_maintenance_extrude, 0);
        }
    }
}
        /****************************************************************************************
         * extrude
         ****************************************************************************************/
static void lcd_menu_maintenance_extrude()
{
    LED_NORMAL();
    if (lcd_lib_encoder_pos / ENCODER_TICKS_PER_SCROLL_MENU_ITEM != 0)
      {
        if (printing_state == PRINT_STATE_NORMAL && movesplanned() < 3)
          {
            current_position[E_AXIS] += lcd_lib_encoder_pos * 0.1;
              //Only move E.
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], EMoveSpeed, active_extruder);
            lcd_lib_encoder_pos = 0;
          }
      }
    if (lcd_lib_button_pressed)
      {
        //        set_extrude_min_temp(EXTRUDE_MINTEMP);
        target_temperature[active_extruder] = 0;
        lcd_change_to_menu(previousMenu, previousEncoderPos, MenuBackward);
      }
    
    lcd_lib_clear();
    lcd_lib_draw_string_centerP(10, PSTR("Nozzle temperature:"));
    lcd_lib_draw_string_centerP(30, PSTR("Press UP to retract"));
    lcd_lib_draw_string_centerP(40, PSTR("Press Down to extrude"));
    lcd_lib_draw_string_centerP(57, PSTR("Click to return"));
    char buffer[16];
    int_to_string(int(current_temperature[active_extruder]), buffer, PSTR("\x81""C/"));
    int_to_string(int(target_temperature[active_extruder]), buffer+strlen(buffer), PSTR("\x81""C"));
    lcd_lib_draw_string_center(20, buffer);
}

    /****************************************************************************************
     * Settings Menu
     *
     ****************************************************************************************/
static char* lcd_advanced_settings_item(uint8_t nr)
{
  if (nr == 0)
      strcpy_P(card.longFilename, PSTR("Return"));
  else if (nr == 1)
      strcpy_P(card.longFilename, PSTR("Retraction settings"));
  else if (nr == 2)
      strcpy_P(card.longFilename, PSTR("Material settings"));
#ifdef FilamentDetection
  else if (nr == 3)
      strcpy_P(card.longFilename, PSTR("Material Detection"));
#endif
  else
      strcpy_P(card.longFilename, PSTR("???"));
  return card.longFilename;
}

static void lcd_advanced_settings_details(uint8_t nr)
{
  if (nr == 0)
      lcd_draw_detailP(PSTR("Return to Advanced."));
  else if (nr == 1)
    {
      lcd_draw_detailP(PSTR("Retraction settings."));
    }
  else if (nr == 2)
    {
      lcd_draw_detailP(PSTR("Material settings."));
    }
#ifdef FilamentDetection
  else if (nr == 3)
    {
      if (isFilamentDetectionEnable) {
          lcd_draw_detailP(PSTR("Detection Sensor  ON"));
      }
      else{
          lcd_draw_detailP(PSTR("Detection Sensor OFF"));
      }
    }
#endif
  else
      lcd_draw_detailP(PSTR("???"));
}

void lcd_menu_advanced_settings()
{
  LED_NORMAL();
#ifdef FilamentDetection
  lcd_advance_menu(PSTR("Settings"), 4, lcd_advanced_settings_item, lcd_advanced_settings_details);
#else
  lcd_advance_menu(PSTR("Settings"), 3, lcd_advanced_settings_item, lcd_advanced_settings_details);
#endif
  
  if (IS_SELECTED_SCROLL(-1)) {
      lcd_change_to_menu(lcd_menu_advanced_movement, SCROLL_MENU_ITEM_POS(2), MenuUp);
  }
  
#ifdef FilamentDetection
  if (IS_SELECTED_SCROLL(4))
#else
      if (IS_SELECTED_SCROLL(3))
#endif
        {
          lcd_change_to_menu(lcd_menu_advanced_about, SCROLL_MENU_ITEM_POS(1), MenuDown);
        }
  
  if (lcd_lib_button_pressed)
    {
      if (IS_SELECTED_SCROLL(0))
          lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(2), MenuBackward);
      else if (IS_SELECTED_SCROLL(1))
        {
          lcd_change_to_menu(lcd_menu_maintenance_retraction, SCROLL_MENU_ITEM_POS(0));
        }
      else if (IS_SELECTED_SCROLL(2))
        {
          lcd_change_to_menu(lcd_menu_material_select, SCROLL_MENU_ITEM_POS(0));
        }
#ifdef FilamentDetection
      else if (IS_SELECTED_SCROLL(3))
        {
          isFilamentDetectionEnable = !isFilamentDetectionEnable;
          Config_StoreSettings();
        }
#endif
    }
  
}

        /****************************************************************************************
         * retraction
         ****************************************************************************************/

static char* lcd_retraction_item(uint8_t nr)
{
    if (nr == 0)
        strcpy_P(card.longFilename, PSTR("Return"));
    else if (nr == 1)
        strcpy_P(card.longFilename, PSTR("Retract length"));
    else if (nr == 2)
        strcpy_P(card.longFilename, PSTR("Retract speed"));
    else if (nr == 3)
        strcpy_P(card.longFilename, PSTR("Recover speed"));
    else
        strcpy_P(card.longFilename, PSTR("???"));
    return card.longFilename;
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
    else if(nr == 3)
        int_to_string(retract_recover_feedrate / 60 + 0.5, buffer, PSTR("mm/sec"));
    lcd_draw_detail(buffer);
    //    lcd_lib_draw_string(5, 57, buffer);
}

static void lcd_menu_maintenance_retraction()
{
    LED_NORMAL();
    lcd_scroll_menu(PSTR("RETRACTION"), 4, lcd_retraction_item, lcd_retraction_details);
    if (lcd_lib_button_pressed)
      {
        if (IS_SELECTED_SCROLL(0))
          {
            Config_StoreSettings();
            lcd_change_to_menu(lcd_menu_advanced_settings, 1, MenuBackward);
          }
        else if (IS_SELECTED_SCROLL(1))
            LCD_EDIT_SETTING_FLOAT001(retract_length, "Retract length", "mm", 0, 50);
        else if (IS_SELECTED_SCROLL(2))
            LCD_EDIT_SETTING_SPEED(retract_feedrate, "Retract speed", "mm/sec", 0, max_feedrate[E_AXIS] * 60);
        else if (IS_SELECTED_SCROLL(3))
            LCD_EDIT_SETTING_SPEED(retract_recover_feedrate, "Recover speed", "mm/sec", 0, max_feedrate[E_AXIS] * 60);
      }
}

    /****************************************************************************************
     * About Menu
     *
     ****************************************************************************************/
static char* lcd_advanced_about_item(uint8_t nr)
{
  if (nr == 0)
      strcpy_P(card.longFilename, PSTR("Return"));
  else if (nr == 1)
      strcpy_P(card.longFilename, PSTR("Factory Reset"));
  else if (nr == 2)
      strcpy_P(card.longFilename, PSTR("Machine State"));
  else if (nr == 3)
      strcpy_P(card.longFilename, PSTR("Version"));
  else
      strcpy_P(card.longFilename, PSTR("???"));
  return card.longFilename;
}

static void lcd_advanced_about_details(uint8_t nr)
{
  if (nr == 0)
      lcd_draw_detailP(PSTR("Return to Advanced."));
  else if (nr == 1)
    {
      lcd_draw_detailP(PSTR("Factory Reset."));
    }
  else if (nr == 2)
    {
      lcd_draw_detailP(PSTR("Machine State."));
    }
  else if (nr == 3)
    {
      lcd_draw_detailP(PSTR(STRING_CONFIG_H_AUTHOR));
    }
  else
      lcd_draw_detailP(PSTR("???"));
}

static void lcd_menu_advanced_about()
{
  LED_NORMAL();
  
  if (lcd_lib_encoder_pos >= 3 * ENCODER_TICKS_PER_SCROLL_MENU_ITEM)
      lcd_lib_encoder_pos = 3 * ENCODER_TICKS_PER_SCROLL_MENU_ITEM;
  
  lcd_advance_menu(PSTR("About"), 4, lcd_advanced_about_item, lcd_advanced_about_details);
  
  if (IS_SELECTED_SCROLL(-1)) {
      lcd_change_to_menu(lcd_menu_advanced_settings, SCROLL_MENU_ITEM_POS(2), MenuUp);
  }
  
  if (lcd_lib_button_pressed)
    {
      if (IS_SELECTED_SCROLL(0))
          lcd_change_to_menu(lcd_menu_maintenance_advanced, SCROLL_MENU_ITEM_POS(3), MenuBackward);
      else if (IS_SELECTED_SCROLL(1))
        {
          lcd_change_to_menu(lcd_menu_advanced_factory_reset, SCROLL_MENU_ITEM_POS(1));
        }
      else if (IS_SELECTED_SCROLL(2))
        {
          lcd_change_to_menu(lcd_menu_advanced_state, SCROLL_MENU_ITEM_POS(0));
        }
      else if (IS_SELECTED_SCROLL(3))
        {
          lcd_change_to_menu(lcd_menu_advanced_version, SCROLL_MENU_ITEM_POS(0));
        }
    }
  
}


        /****************************************************************************************
         * version
         ****************************************************************************************/
void lcd_menu_advanced_version()
{
  char eepromBuffer[9]={0};
    if (lcd_lib_encoder_pos>=2) {
        doAdvancedLevel();
        lcd_change_to_menu(lcd_menu_advanced_level);
    }
    
    LED_NORMAL();
    lcd_info_screen(lcd_menu_advanced_about, NULL, PSTR("Return"));
    nextEncoderPos=3;
  
    eeprom_read_block(eepromBuffer, (uint8_t*)EEPROM_DEVICE_ID, 8);
  
    lcd_lib_draw_string_centerP(10, PSTR(STRING_VERSION_CONFIG_H));
    lcd_lib_draw_string_centerP(20, PSTR(STRING_CONFIG_H_AUTHOR));
    lcd_lib_draw_string_centerP(30, PSTR("Device ID:"));
    lcd_lib_draw_string_center(40, eepromBuffer);
}

#define SCREW_NORMAL 0
#define SCREW_UP 1
#define SCREW_DOWN 2

#define SCREW_STATE_FIRST 0
#define SCREW_STATE_TOUCH 1
#define SCREW_STATE_UNTOUCH 2

static uint8_t touchState=SCREW_STATE_FIRST;

static void doAdvancedLevel()
{
    enable_endstops(true);
    
    touchState=SCREW_STATE_FIRST;
    
    current_position[X_AXIS]=0;
    current_position[Y_AXIS]=0;
    current_position[Z_AXIS]=0;
    current_position[E_AXIS]=0;
    //Only move Z.
    plan_set_position_old(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS]);
    
    current_position[X_AXIS]=MANUAL_Z_HOME_POS;
    current_position[Y_AXIS]=MANUAL_Z_HOME_POS;
    current_position[Z_AXIS]=MANUAL_Z_HOME_POS;
    current_position[E_AXIS]=0;
    feedrate=4000;
    
    plan_buffer_line_old(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], feedrate/60 , active_extruder);
}

static void doAdvancedLevelDone()
{
    endstops_hit_on_purpose();
}

static void lcd_menu_advanced_level()
{
    lcd_info_screen(lcd_menu_advanced_version, doAdvancedLevelDone, PSTR("DONE"));
    
    char buffer[16];
    int_to_string(int(st_get_position(X_AXIS)), buffer, NULL);
    lcd_lib_draw_string_center(10, buffer);
    
    int_to_string(int(READ(X_MAX_PIN)), buffer, PSTR(" "));
    int_to_string(int(READ(Y_MAX_PIN)), buffer+strlen(buffer), PSTR(" "));
    int_to_string(int(READ(Z_MAX_PIN)), buffer+strlen(buffer), PSTR(" "));
    lcd_lib_draw_string_center(20, buffer);
    
    static unsigned long advancedLevelTimer=millis();
    static uint8_t xState=SCREW_NORMAL;
    static uint8_t yState=SCREW_NORMAL;
    static uint8_t zState=SCREW_NORMAL;

    switch (xState) {
        case SCREW_NORMAL:
            lcd_lib_draw_stringP(0, 40, PSTR("O"));
            break;
        case SCREW_UP:
            lcd_lib_draw_stringP(0, 40, PSTR("\x82"));
            break;
        case SCREW_DOWN:
            lcd_lib_draw_stringP(0, 40, PSTR("\x83"));
            break;
        default:
            break;
    }
    
    switch (yState) {
        case SCREW_NORMAL:
            lcd_lib_draw_stringP(100, 40, PSTR("O"));
            break;
        case SCREW_UP:
            lcd_lib_draw_stringP(100, 40, PSTR("\x82"));
            break;
        case SCREW_DOWN:
            lcd_lib_draw_stringP(100, 40, PSTR("\x83"));
            break;
        default:
            break;
    }
    
    switch (zState) {
        case SCREW_NORMAL:
            lcd_lib_draw_stringP(50, 30, PSTR("O"));
            break;
        case SCREW_UP:
            lcd_lib_draw_stringP(50, 30, PSTR("\x82"));
            break;
        case SCREW_DOWN:
            lcd_lib_draw_stringP(50, 30, PSTR("\x83"));
            break;
        default:
            break;
    }
    
    
    if (blocks_queued()) {
        advancedLevelTimer=millis();
    }
    
    if (millis()-advancedLevelTimer>300) {
        advancedLevelTimer=millis();
        
        
        switch (touchState) {
            case SCREW_STATE_FIRST:
                if (st_get_position(X_AXIS)>=(lround(STEPS_PER_UNIT_DELTA*261))) {
                    enable_endstops(false);
                    touchState=SCREW_STATE_TOUCH;
                }
                else{
                    
                    if (!READ(X_MAX_PIN)) {// touched
                        xState=SCREW_DOWN;
                        yState=SCREW_NORMAL;
                        zState=SCREW_NORMAL;
                    }
                    
                    if (!READ(Y_MAX_PIN)) {// touched
                        xState=SCREW_NORMAL;
                        yState=SCREW_DOWN;
                        zState=SCREW_NORMAL;
                    }
                    
                    if (!READ(Z_MAX_PIN)) {// touched
                        xState=SCREW_NORMAL;
                        yState=SCREW_NORMAL;
                        zState=SCREW_DOWN;
                    }
                    
                    
                    current_position[X_AXIS]=(st_get_position(X_AXIS))/STEPS_PER_UNIT_DELTA;
                    current_position[Y_AXIS]=current_position[X_AXIS];
                    current_position[Z_AXIS]=current_position[X_AXIS];
                    current_position[E_AXIS]=0;
                    plan_set_position_old(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS]);
                    
                    if ((READ(X_MAX_PIN))&&(READ(Y_MAX_PIN))&&(READ(Z_MAX_PIN))) {
                        current_position[X_AXIS]=262;
                        current_position[Y_AXIS]=262;
                        current_position[Z_AXIS]=262;
                        current_position[E_AXIS]=0;
                        feedrate=4000;
                    }
                }
                break;
                
            case SCREW_STATE_TOUCH:
                
                if (READ(X_MAX_PIN)) {//not touched
                    xState=SCREW_UP;
                }
                else{
                    if (xState==SCREW_UP) {
                        xState=SCREW_NORMAL;
                    }
                }
                
                if (READ(Y_MAX_PIN)) {//not touched
                    yState=SCREW_UP;
                }
                else{
                    if (yState==SCREW_UP) {
                        yState=SCREW_NORMAL;
                    }
                }
                
                if (READ(Z_MAX_PIN)) {//not touched
                    zState=SCREW_UP;
                }
                else{
                    if (zState==SCREW_UP) {
                        zState=SCREW_NORMAL;
                    }
                }
                
                current_position[X_AXIS]=261.5;
                current_position[Y_AXIS]=261.5;
                current_position[Z_AXIS]=261.5;
                current_position[E_AXIS]=0;
                feedrate=4000;
                touchState=SCREW_STATE_UNTOUCH;
                break;
                
            case SCREW_STATE_UNTOUCH:
                
                if (READ(X_MAX_PIN)) {//not touched
                    if (xState==SCREW_DOWN) {
                        xState=SCREW_NORMAL;
                    }
                }
                else{
                    xState=SCREW_DOWN;
                }
                
                if (READ(Y_MAX_PIN)) {//not touched
                    if (yState==SCREW_DOWN) {
                        yState=SCREW_NORMAL;
                    }
                }
                else{
                    yState=SCREW_DOWN;
                }
                
                if (READ(Z_MAX_PIN)) {//not touched
                    if (zState==SCREW_DOWN) {
                        zState=SCREW_NORMAL;
                    }
                }
                else{
                    zState=SCREW_DOWN;
                }
                
                current_position[X_AXIS]=262;
                current_position[Y_AXIS]=262;
                current_position[Z_AXIS]=262;
                current_position[E_AXIS]=0;
                feedrate=4000;
                touchState=SCREW_STATE_TOUCH;
                
                break;
                
            default:
                break;
        }
        //Only move z.
        plan_buffer_line_old(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], feedrate/60 , active_extruder);
    }
}

        /****************************************************************************************
         * state
         ****************************************************************************************/
static void lcd_menu_advanced_state()
{
    LED_NORMAL();
    lcd_info_screen(previousMenu, NULL, PSTR("Return"));
    nextEncoderPos=2;
    if (READ(BatteryPin)) {
        lcd_lib_draw_stringP(5, 10, PSTR("Battery:    Charging"));
    }
    else{
        lcd_lib_draw_stringP(5, 10, PSTR("Battery:     Charged"));
    }
    
    char buffer[32];
    char* c;
    
    lcd_lib_draw_stringP(5, 20, PSTR("Life time:"));
    c = buffer;
    c = int_to_string(lifetime_minutes / 60, c, PSTR(":"));
    if (lifetime_minutes % 60 < 10)
        *c++ = '0';
    c = int_to_string(lifetime_minutes % 60, c);
    
    lcd_lib_draw_string((21-strlen(buffer))*6-1, 20, buffer);
    
    
    lcd_lib_draw_stringP(5, 30, PSTR("Print time:"));
    c = buffer;
    c = int_to_string(lifetime_print_minutes / 60, c, PSTR(":"));
    if (lifetime_print_minutes % 60 < 10)
        *c++ = '0';
    c = int_to_string(lifetime_print_minutes % 60, c);
    lcd_lib_draw_string((21-strlen(buffer))*6-1, 30, buffer);

    lcd_lib_draw_stringP(5, 40, PSTR("Material:"));
    c = buffer;
    c = int_to_string(lifetime_print_centimeters / 100, c, PSTR("m"));
    lcd_lib_draw_string((21-strlen(buffer))*6-1, 40, buffer);
}

        /****************************************************************************************
         * Factory Reset
         ****************************************************************************************/
static void doFactoryReset()
{
    //Clear the EEPROM settings so they get read from default.
    eeprom_write_byte((uint8_t*)100, 0);
    eeprom_write_byte((uint8_t*)101, 0);
    eeprom_write_byte((uint8_t*)102, 0);
    eeprom_write_byte((uint8_t*)EEPROM_FIRST_RUN_DONE_OFFSET, 0);
    eeprom_write_byte(EEPROM_MATERIAL_COUNT_OFFSET(), 0);
    
    cli();
    //NOTE: Jumping to address 0 is not a fully proper way to reset.
    // Letting the watchdog timeout is a better reset, but the bootloader does not continue on a watchdog timeout.
    // So we disable interrupts and hope for the best!
    //Jump to address 0x0000
#ifdef __AVR__
    asm volatile(
                 "clr	r30		\n\t"
                 "clr	r31		\n\t"
                 "ijmp	\n\t"
                 );
#else
    //TODO
#endif
}

static void lcd_menu_advanced_factory_reset()
{
    LED_GLOW();
    lcd_question_screen(NULL, doFactoryReset, PSTR("YES"), previousMenu, NULL, PSTR("NO"));
    nextEncoderPos=1;
    lcd_lib_draw_string_centerP(10, PSTR("Reset everything"));
    lcd_lib_draw_string_centerP(20, PSTR("to default?"));
}

#endif//ENABLE_ULTILCD2
