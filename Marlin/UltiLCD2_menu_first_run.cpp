#include <avr/pgmspace.h>

#include "Configuration.h"
#ifdef ENABLE_ULTILCD2
#include "Marlin.h"
#include "cardreader.h"//This code uses the card.longFilename as buffer to store data, to save memory.
#include "temperature.h"
#include "ConfigurationStore.h"
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "UltiLCD2_menu_material.h"
#include "UltiLCD2_menu_first_run.h"
#include "UltiLCD2_menu_print.h"
#include "UltiLCD2_menu_maintenance.h"
#include "fitting_bed.h"

static void lcd_menu_first_run_print_card_detect();
static void lcd_menu_first_run_print();
static void setFirstRunDone();
static void lcd_menu_first_run_material_load_wait();
static void doAfterMaterialLoad();
static void lcd_menu_first_run_material_load_forward();
static void lcd_menu_first_run_material_load_insert();
static void runMaterialForward();
static void lcd_menu_first_run_material_load_heatup();
static void lcd_menu_first_run_material_load();
static void doMaterialReset();
static void lcd_menu_first_run_waiting_auto_level();
static void lcd_menu_first_run_heat_for_level();
static void doCancelAutoLevel();
static void skipAll();

static unsigned long waitTimer;


#define DRAW_PROGRESS_NR(nr) do { lcd_lib_draw_stringP((nr < 10) ? 100 : 94, 0, PSTR( #nr "/10")); } while(0)

//Run the first time you start-up the machine or after a factory reset.

/****************************************************************************************
 * Welcome page
 *
 ****************************************************************************************/
static void skipAll()
{
    Config_ResetDefault();
    doCancelAutoLevel();
    doMaterialReset();
    setFirstRunDone();
}

void lcd_menu_first_run_init()
{
  LED_NORMAL();
  lcd_question_screen(lcd_menu_first_run_heat_for_level, NULL, PSTR("CONTINUE"), lcd_menu_main, skipAll, PSTR("DEFAULT"),MenuForward,MenuForward);
  DRAW_PROGRESS_NR(1);
  lcd_lib_draw_string_centerP(10, PSTR("Welcome to the first"));
  lcd_lib_draw_string_centerP(20, PSTR("startup of OverLord!"));
  lcd_lib_draw_string_centerP(30, PSTR("Press middle button"));
  lcd_lib_draw_string_centerP(40, PSTR("to continue"));
}

/****************************************************************************************
 * Auto level waiting heating
 *
 ****************************************************************************************/
static void doCancelAutoLevel()
{
    fittingBedReset();
    fittingBedResetK();
    fittingBedOffsetInit();
    add_homeing[Z_AXIS] = 0;
    setTargetHotend(0, 0);
    Config_StoreSettings();
}

static void lcd_menu_first_run_heat_for_level()
{
    LED_GLOW_HEAT();
    setTargetHotend(160, 0);
    int16_t temp = degHotend(0) - 20;
    int16_t target = degTargetHotend(0) - 10 - 20;
    if (temp < 0) temp = 0;
    if (temp > target){
        setTargetHotend(0, 0);
        setTargetBed(0);
        enquecommand_P(PSTR("G29"));
        currentMenu = lcd_menu_first_run_waiting_auto_level;
        temp = target;
    }
    
    uint8_t progress = uint8_t(temp * 125 / target);
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;
    
    lcd_info_screen(lcd_menu_first_run_material_load, doCancelAutoLevel, PSTR("CANCEL"), MenuForward);
    DRAW_PROGRESS_NR(2);
    lcd_lib_draw_string_centerP(10, PSTR("Printhead heating in"));
    lcd_lib_draw_string_centerP(20, PSTR("case that material"));
    lcd_lib_draw_string_centerP(30, PSTR("is left around it."));
    
    lcd_progressbar(progress);
}


/****************************************************************************************
 * waiting Auto leveling finishing
 *
 ****************************************************************************************/
static void doCancelAutoLevelProcession()
{
    doCancelAutoLevel();
    printing_state=PRINT_STATE_HOMING_ABORT;
}

static void lcd_menu_first_run_waiting_auto_level()
{
    LED_NORMAL();
    if (printing_state!=PRINT_STATE_NORMAL || is_command_queued() || isCommandInBuffer()) {
        lcd_info_screen(lcd_menu_first_run_material_load, doCancelAutoLevelProcession, PSTR("ABORT"), MenuForward);
    }
    else{
        lcd_change_to_menu(lcd_menu_first_run_material_load,0,MenuForward);
    }
    
    DRAW_PROGRESS_NR(3);
    lcd_lib_draw_string_centerP(10, PSTR("The Nozzle will"));
    lcd_lib_draw_string_centerP(20, PSTR("touch the buildplate"));
    lcd_lib_draw_string_centerP(30, PSTR("gently to do the"));
    lcd_lib_draw_string_centerP(40, PSTR("calibration process."));
}


/****************************************************************************************
 * Insert material
 *
 ****************************************************************************************/

static void doMaterialReset()
{
  lcd_material_reset_defaults();
  for(uint8_t e=0; e<EXTRUDERS; e++)
    lcd_material_set_material(0, e);
}

static void lcd_menu_first_run_material_load()
{
  LED_NORMAL();
  lcd_question_screen(lcd_menu_first_run_material_load_heatup, doMaterialReset, PSTR("CONTINUE"), lcd_menu_first_run_print, doMaterialReset, PSTR("SKIP"),MenuForward,MenuForward);
  DRAW_PROGRESS_NR(4);
  lcd_lib_draw_string_centerP(10, PSTR("Next step is to"));
  lcd_lib_draw_string_centerP(20, PSTR("insert material."));
  lcd_lib_draw_string_centerP(30, PSTR("push middle button"));
  lcd_lib_draw_string_centerP(40, PSTR("to continue."));
}

/****************************************************************************************
 * Insert material preheat
 *
 ****************************************************************************************/
static void lcd_menu_first_run_material_load_heatup()
{
  LED_GLOW_HEAT();
  setTargetHotend(220, 0);
  int16_t temp = degHotend(0) - 20;
  int16_t target = degTargetHotend(0) - 10 - 20;
  if (temp < 0) temp = 0;
  if (temp > target)
  {
    for(uint8_t e=0; e<EXTRUDERS; e++)
      volume_to_filament_length[e] = 1.0;//Set the extrusion to 1mm per given value, so we can move the filament a set distance.
    
    plan_set_position(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS],current_position[E_AXIS]);
    currentMenu = lcd_menu_first_run_material_load_insert;
    waitTimer = millis();
    temp = target;
  }
  
  uint8_t progress = uint8_t(temp * 125 / target);
  if (progress < minProgress)
    progress = minProgress;
  else
    minProgress = progress;
  
  lcd_basic_screen();
  DRAW_PROGRESS_NR(5);
  lcd_lib_draw_string_centerP(10, PSTR("Please wait,"));
  lcd_lib_draw_string_centerP(20, PSTR("printhead heating for"));
  lcd_lib_draw_string_centerP(30, PSTR("material loading"));
  
  lcd_progressbar(progress);
}

/****************************************************************************************
 * Insert material first inserting
 *
 ****************************************************************************************/
static void runMaterialForward()
{
  LED_NORMAL();
  //Override the max feedrate and acceleration values to get a better insert speed and speedup/slowdown
  float old_max_feedrate_e = max_feedrate[E_AXIS];
  float old_retract_acceleration = retract_acceleration;
  max_feedrate[E_AXIS] = FILAMENT_INSERT_FAST_SPEED;
  retract_acceleration = FILAMENT_LONG_MOVE_ACCELERATION;
  
  current_position[E_AXIS] = 0;
  plan_set_position(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS],current_position[E_AXIS]);
  for(uint8_t n=0;n<6;n++)
  {
    current_position[E_AXIS] += FILAMENT_FORWARD_LENGTH  / volume_to_filament_length[active_extruder] / 6;
      //Only move E.
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], FILAMENT_INSERT_FAST_SPEED, 0);
  }
  
  //Put back origonal values.
  max_feedrate[E_AXIS] = old_max_feedrate_e;
  retract_acceleration = old_retract_acceleration;
}

static void lcd_menu_first_run_material_load_insert()
{
  LED_GLOW();
    
    static bool isForwardState = true;
    
    if (printing_state == PRINT_STATE_NORMAL && movesplanned() < 2)
    {
        if (isForwardState) {
            isForwardState = false;
            current_position[E_AXIS] += 10;
        }
        else{
            isForwardState = true;
            current_position[E_AXIS] -= 9.9;
        }
        
        //Only move E.
        plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], FILAMENT_INSERT_SPEED, active_extruder);
    }
    
    lcd_info_screen(lcd_menu_first_run_material_load_forward, runMaterialForward, PSTR("READY   "),MenuForward);
    
    
    
    char buffer[10];
    char* c = buffer;
    
    int leftTime = FILAMENT_INSERT_TIME - (millis()-waitTimer)/1000;
    leftTime = constrain(leftTime, 0, FILAMENT_INSERT_TIME);
    
    int_to_string(leftTime, buffer);
    
    lcd_lib_clear_string(65 - strlen_P(PSTR("READY   ")) * 3 + 6 * 6, 56, buffer);
    
    if (leftTime == 0) {
        lcd_change_to_menu(lcd_menu_first_run_material_load_forward);
        runMaterialForward();
    }
  
  //    SELECT_MAIN_MENU_ITEM(0);
//  lcd_info_screen(lcd_menu_first_run_material_load_forward, runMaterialForward, PSTR("CONTINUE"),MenuForward);
  DRAW_PROGRESS_NR(6);
  lcd_lib_draw_string_centerP(10, PSTR("Insert material into"));
  lcd_lib_draw_string_centerP(20, PSTR("extruder until it is"));
  lcd_lib_draw_string_centerP(30, PSTR("drived by extruder"));
  lcd_lib_draw_string_centerP(40, PSTR("and then push button."));
}

/****************************************************************************************
 * Insert material fast inserting
 *
 ****************************************************************************************/
static void lcd_menu_first_run_material_load_forward()
{
  LED_NORMAL();
  lcd_basic_screen();
  DRAW_PROGRESS_NR(7);
  lcd_lib_draw_string_centerP(20, PSTR("Loading material..."));
  
  if (!blocks_queued())
  {
    lcd_lib_beep();
    led_glow_dir = led_glow = 0;
#if MOTOR_CURRENT_PWM_XY_PIN > -1
    digipot_current(2, motor_current_setting[2]*2/3);//Set E motor power lower so the motor will skip instead of grind.
#endif
    currentMenu = lcd_menu_first_run_material_load_wait;
    waitTimer = millis();
    SELECT_MAIN_MENU_ITEM(0);
  }
  
  long pos = st_get_position(E_AXIS);
  long targetPos = lround(FILAMENT_FORWARD_LENGTH*axis_steps_per_unit[E_AXIS]);
  uint8_t progress = (pos * 125 / targetPos);
  lcd_progressbar(progress);
}

/****************************************************************************************
 * Insert material last inserting
 *
 ****************************************************************************************/
static void doAfterMaterialLoad()
{
    current_position[E_AXIS] = END_OF_PRINT_RETRACTION / volume_to_filament_length[active_extruder];
    plan_set_position(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS],current_position[E_AXIS]);
    current_position[E_AXIS] = 0;
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], retract_feedrate/60, 0);
    clearPrimed();
    enquecommand_P(PSTR("M84"));
    setTargetHotend(0, 0);
}

static void lcd_menu_first_run_material_load_wait()
{
  LED_GLOW();
  
  lcd_info_screen(lcd_menu_first_run_print, doAfterMaterialLoad, PSTR("READY   "), MenuForward);
  DRAW_PROGRESS_NR(8);
  lcd_lib_draw_string_centerP(10, PSTR("Push button when"));
  lcd_lib_draw_string_centerP(20, PSTR("material exits"));
  lcd_lib_draw_string_centerP(30, PSTR("from nozzle..."));
  
    
    
    char buffer[10];
    char* c = buffer;
    
    int leftTime = FILAMENT_INSERT_TIME - (millis()-waitTimer)/1000;
    leftTime = constrain(leftTime, 0, FILAMENT_INSERT_TIME);
    
    int_to_string(leftTime, buffer);
    
    lcd_lib_clear_string(65 - strlen_P(PSTR("READY   ")) * 3 + 6 * 6, 56, buffer);
    
    if (leftTime == 0) {
        lcd_change_to_menu(lcd_menu_first_run_print);
        doAfterMaterialLoad();
    }
    
  if (movesplanned() < 2)
  {
    current_position[E_AXIS] += 0.5;
      //Only move E.
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], FILAMENT_INSERT_EXTRUDE_SPEED, 0);
  }
}


/****************************************************************************************
 * Insert material last inserting
 *
 ****************************************************************************************/
static void setFirstRunDone()
{
  SET_FIRST_RUN_DONE();
    lcd_clear_cache();
}

static void lcd_menu_first_run_print()
{
  LED_NORMAL();
  lcd_question_screen(lcd_menu_first_run_print_card_detect, NULL, PSTR("CONTINUE"), lcd_menu_main, setFirstRunDone, PSTR("SKIP"), MenuForward, MenuForward);
  
  DRAW_PROGRESS_NR(9);
  lcd_lib_draw_string_centerP(20, PSTR("I'm ready. let's"));
  lcd_lib_draw_string_centerP(30, PSTR("make a 3D Print!"));
}

/****************************************************************************************
 * card detect
 *
 ****************************************************************************************/
static void lcd_menu_first_run_print_card_detect()
{
  LED_NORMAL();
  if (!card.sdInserted)
  {
    LED_GLOW();
    lcd_info_screen(lcd_menu_main,NULL,NULL, MenuForward);
    DRAW_PROGRESS_NR(10);
    lcd_lib_draw_string_centerP(20, PSTR("Please insert SD-card"));
    lcd_lib_draw_string_centerP(30, PSTR("that came with"));
    lcd_lib_draw_string_centerP(40, PSTR("your OverLord..."));
    return;
  }
  
  if (!card.isOk())
  {
    lcd_info_screen(lcd_menu_main,NULL,NULL, MenuForward);
    DRAW_PROGRESS_NR(10);
    lcd_lib_draw_string_centerP(30, PSTR("Reading card..."));
    return;
  }
  
  //    SELECT_MAIN_MENU_ITEM(0);
  lcd_info_screen(lcd_menu_print_select, setFirstRunDone, PSTR("LET'S PRINT"), MenuForward);
  DRAW_PROGRESS_NR(10);
  lcd_lib_draw_string_centerP(10, PSTR("Select a print file"));
  lcd_lib_draw_string_centerP(20, PSTR("on the SD-card"));
  lcd_lib_draw_string_centerP(30, PSTR("and press the button"));
  lcd_lib_draw_string_centerP(40, PSTR("to print it!"));
}
#endif//ENABLE_ULTILCD2
