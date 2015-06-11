#ifndef __RAM_CORR_TEST_H__

#define __RAM_CORR_TEST_H__

#include <inttypes.h>

#define RamTestBlockSize 6049

extern volatile uint8_t RamTestBlock[RamTestBlockSize];

extern void RamCorrTestInit();

extern void RamCorrTestStep();
	
extern void RamCorrTestDebugPrint();

#endif //ndef __RAM_CORR_TEST_H__
