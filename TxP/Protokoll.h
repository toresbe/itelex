#ifndef __PROTOKOLL_H__

#define __PROTOKOLL_H__

#include "stdbool.h"

extern bool ProtokollSpeichern();

extern void Protokollieren(char *s);

extern void Protokollieren_P(const char *s);

extern void ProtokollierenInt_P(const char *s, long i);

extern void ProtokollInit();

#endif //ndef __PROTOKOLL_H__