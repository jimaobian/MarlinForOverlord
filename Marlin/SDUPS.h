
#ifndef SDUPS_h
#define SDUPS_h

#ifdef SDUPS


#include "ConfigurationStore.h"
#define SDUPSValidateSize 64
#define SDUPSPositionSize 64

bool SDUPSRetrieveClass();
void SDUPSScanPosition();
uint32_t SDUPSRetrievePosition(int8_t theIndex);
void SDUPSStorePosition(uint32_t theFilePosition);

boolean SDUPSIsWorking();

void SDUPSStart();

void SDUPSDone();

#endif

#endif
