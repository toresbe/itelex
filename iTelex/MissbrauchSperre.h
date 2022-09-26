/*
 * MissbrauchSperre.h
 *
 * Created: 22.09.2022 18:49:08
 *  Author: sonne-fr
 */ 


#ifndef __MISSBRAUCH_SPERRE_H__

#define __MISSBRAUCH_SPERRE_H__


extern void MissSperrInit();

extern void MissSperrZeichen(uint8_t code, bool AnFernschr);

extern void MissSperrStartVerbindung();

extern void MissSperrEndeVerbindung();


#endif