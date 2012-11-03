#ifndef __PROTOKOLL_H__

#define __PROTOKOLL_H__

#include <stdbool.h>

extern uint8_t ProtokollLevel;

extern uint8_t ProtokollLevelTlnServ;

extern bool ProtokollSpeichern(bool flush);

extern void Protokollieren(char *s);

extern void Protokollieren_P(const prog_char *s);

extern void ProtokollierenInt_P(const prog_char *s, long i);

extern void ProtokollierenIPAdr(long aip);

extern void ProtokollierenMAC(char mac[6]);

extern void ProtokollInit();

#endif //ndef __PROTOKOLL_H__