/*! \file RamCorrTest.c \brief Test auf Daten-Korruptiuon durch fehlerhaften Code */

#include <avr/pgmspace.h>
#include <avr/version.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bool.h>

#include "config.h"

volatile uint8_t RamTestBlock[6049];

#define RamTestBlockSize (sizeof(RamTestBlock) / sizeof(uint8_t))

static uint16_t RamTestPos;

static uint8_t RamTestValue;

static uint16_t RamTestErrorCounter;

static char RamTestResult[100];


void RamCorrTestInit()
	{
	uint16_t i;
	
	RamTestValue = 0;
	for (i = 0 ; i < RamTestBlockSize ; i++)
		RamTestBlock[i] = RamTestValue;
	RamTestPos = 0;
	RamTestErrorCounter = 0;
	RamTestResult[0] = '\0';
	}
	
	
void RamCorrTestStep()
	{
	if (RamTestBlock[RamTestPos] == RamTestValue)
		{ // ok
		if (RamTestValue == 0xFF)
			{ // nach FF kommt wieder 0
			RamTestBlock[RamTestPos] = 0;
			RamTestPos++;
			if (RamTestPos >= RamTestBlockSize)
				{
				RamTestPos = 0;
				RamTestValue = 0;
				}
			}
		else // RamTestValue < 0xFF
			{ 
			RamTestBlock[RamTestPos] = RamTestValue + 1;
			RamTestPos++;
			if (RamTestPos >= RamTestBlockSize)
				{
				RamTestPos = 0;
				RamTestValue++;
				}
			}
		}
	else // RamTestBlock[RamTestPos] != RamTestValue
		{ 
		RamTestErrorCounter++;
		
		RamTestResult[sizeof(RamTestResult)-1] = '\0'; // to be VERY sure that this string is limited in length...
		
		uint8_t len = strlen(RamTestResult);
		
		// enough space left in Result string?
		if (len + 15 > sizeof(RamTestResult))
			{
			strcpy(RamTestResult, RamTestResult + 15);
			len = strlen(RamTestResult);
			}
		
		sprintf(RamTestResult + len, PSTR("%04X:%02X>%02X "), (uint16_t) RamTestBlock + RamTestPos, RamTestValue, RamTestBlock[RamTestPos]);
		
		RamTestBlock[RamTestPos] = RamTestValue;
		}
		
	} // RamCorrTestStep()
	
	
void RamCorrTestDebugPrint()
	{
	printf_P(PSTR("<br>RamTest status: Pos %d Val %d"), RamTestPos, RamTestValue);
	printf_P(PSTR("<br>RamTest restult: error counter: %d  error pos: "), RamTestErrorCounter);
	printf(RamTestResult);
	}
	
	
