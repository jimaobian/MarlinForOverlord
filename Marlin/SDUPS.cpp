#include "Configuration.h"

#ifdef SDUPS
#include "Marlin.h"
#include "SDUPS.h"
#include "cardreader.h"
#include "temperature.h"
#include "UltiLCD2_menu_print.h"

//uint32_t SDUPSFilePosition=0;


static int8_t SDUPSMaxIndex=-1,SDUPSMinIndex=-1;
static uint8_t SDUPSPositionIndex=0;


boolean SDUPSIsWorking()
{
    return(eeprom_read_byte((const uint8_t*)EEPROM_SDUPS_WORKING_OFFSET) == 'H');
}

void SDUPSStart()
{
    eeprom_write_byte((uint8_t*)EEPROM_SDUPS_WORKING_OFFSET, 'H');
    SDUPSPositionIndex=((uint8_t)millis())%SDUPSPositionSize;
}

void SDUPSDone()
{
    eeprom_write_byte((uint8_t*)EEPROM_SDUPS_WORKING_OFFSET, 'L');
}


void SDUPSStorePosition(uint32_t theFilePosition)
{
    
    eeprom_write_dword(((uint32_t *)EEPROM_SDUPSPositionOffset) + SDUPSPositionIndex, theFilePosition);
    
    SDUPSPositionIndex=(SDUPSPositionIndex+1)% SDUPSPositionSize;

    SERIAL_DEBUGLNPGM("SDUPSPositionIndex:");
    SERIAL_DEBUGLN((int)SDUPSPositionIndex);
    SERIAL_DEBUGLNPGM("the FilePosition:");
    SERIAL_DEBUGLN(theFilePosition);
}

void SDUPSScanPosition()
{
    SDUPSMaxIndex=0;
    SDUPSMinIndex=0;
    uint32_t maxBuffer=0,minBuffer=0xffffffffUL,eepromReadBuffer=0;

    for (int i=0; i<SDUPSPositionSize; i++) {
        
        eepromReadBuffer=eeprom_read_dword((uint32_t *)(EEPROM_SDUPSPositionOffset)+i);
        
        if (eepromReadBuffer) {
            if (eepromReadBuffer>maxBuffer) {
                maxBuffer=eepromReadBuffer;
                SDUPSMaxIndex=i;
            }
            if (eepromReadBuffer<minBuffer) {
                minBuffer=eepromReadBuffer;
                SDUPSMinIndex=i;
            }
        }
    }
    
    SDUPSPositionIndex=(SDUPSMaxIndex+1)% SDUPSPositionSize;
    
    SERIAL_DEBUGLNPGM("SDUPSScanIndex");
    SERIAL_DEBUGLN(SDUPSMaxIndex);
    SERIAL_DEBUGLN(SDUPSMinIndex);
    
    SERIAL_DEBUGLNPGM("SDUPSScanPosition:");
    SERIAL_DEBUGLN(eeprom_read_dword((uint32_t *)(EEPROM_SDUPSPositionOffset)+SDUPSMaxIndex));
    SERIAL_DEBUGLN(eeprom_read_dword((uint32_t *)(EEPROM_SDUPSPositionOffset)+SDUPSMinIndex));


}

uint32_t SDUPSRetrievePosition(int8_t theIndex)
{
    uint32_t eepromReadBuffer;
    
    if (theIndex>=-SDUPSPositionSize && theIndex<0){
        theIndex=(SDUPSMinIndex-theIndex-1)%SDUPSPositionSize;
        eepromReadBuffer=eeprom_read_dword((uint32_t *)(EEPROM_SDUPSPositionOffset)+theIndex);
        if (eepromReadBuffer==0) {
            eepromReadBuffer=0xffffffffUL;;
        }
    }
    else if (theIndex<=SDUPSPositionSize && theIndex>0){
        theIndex=(SDUPSMaxIndex-theIndex+1+SDUPSPositionSize)%SDUPSPositionSize;
        eepromReadBuffer=eeprom_read_dword((uint32_t *)(EEPROM_SDUPSPositionOffset)+theIndex);
        if (eepromReadBuffer==0) {
            eepromReadBuffer=0xffffffffUL;;
        }
    }
    else{
        eepromReadBuffer=0xffffffffUL;;
    }
    
    SERIAL_DEBUGLN("SDUPSRetrievePosition");
    SERIAL_DEBUGLN(eepromReadBuffer);
    
    return eepromReadBuffer;
}


bool SDUPSRetrieveClass()
{
    uint16_t SDUPSValidate;
    
    uint16_t SDUPSValidateValue;
    
  
    uint8_t cardErrorTimes=0;
    
    int i=EEPROM_SDUPSClassOffset;
    
    EEPROM_READ_VAR(i, card);
    EEPROM_READ_VAR(i, SDUPSValidate);
    EEPROM_READ_VAR(i, lcd_cache);
  
    uint32_t lastFilePosition=card.getFilePos();
  
    do {
        card.clearError();
        SDUPSValidateValue=0;
        cardErrorTimes++;
        
        card.setIndex(card.getFileSize()/2UL);

        
        for (int j=0; j<SDUPSValidateSize; j++) {
            SDUPSValidateValue += (uint8_t)(card.get());
        }
    } while (card.errorCode() && cardErrorTimes<5);
    
    card.setIndex(lastFilePosition);
    SERIAL_DEBUGLNPGM("SDUPSRetrieveClass:");
    SERIAL_DEBUGLN(SDUPSValidateValue);
    SERIAL_DEBUGLN(SDUPSValidate);
    
    return (SDUPSValidateValue==SDUPSValidate);
    
}

#endif