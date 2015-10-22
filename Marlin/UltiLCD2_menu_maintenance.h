#ifndef ULTI_LCD2_MENU_MAINTENANCE_H
#define ULTI_LCD2_MENU_MAINTENANCE_H

void lcd_menu_maintenance();
void lcd_menu_advanced_settings();
#if TEMP_SENSOR_BED != 0
void lcd_menu_maintenance_advanced_bed_heatup();//TODO
#endif
void manualLevelRoutine();
void lcd_menu_advanced_version();


#define EMoveSpeed  10

#endif//ULTI_LCD2_MENU_MAINTENANCE_H
