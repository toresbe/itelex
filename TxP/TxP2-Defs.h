// TxP2-Übergreifende definitionen
// ===============================

// I²C-Adressen
// ------------

// Meldungsnummer für Verbindungsaufnahme = BusAdr / 2 !

#define BusAdrMin (1*2) // = 0x02
#define BusAdrEndgeraetMax (100*2) // = 0xC8
#define BusAdrMax (110*2)  // = 0xDC
#define BusAdrFirmwareLoad 238    // = 0xEE
#define BusAdrUngueltig 255 	  // = 0xFF

//#define MaxLeitungen ((BusAdrLeitungMax - BusAdrLeitungMin) / 2 + 1)


// I²C-Meldungen
// -------------

#define BusKdoVerbAufnahme	0x7F // maximum
#define BusKdoEin			0xA3
#define BusQuittEin			0xA5
#define BusKdoWahlFreigabe 	0xA6
#define BusKdoWahlziffer0	0xB0
#define BusKdoWahlziffer9	0xB9
#define BusLebenszeichen	0xA9
#define BusKdoSchluss		0xAA
#define BusQuittSchluss		0xAC
#define BusKdoSpace			0x93
#define BusKdoMark			0x9C
#define BusKdoSpaceWdh		0x96
#define BusKdoMarkWdh		0x9A

/* überholt...
#define BusQuittKonfig		0xAF // wird vom Modem o.ä. statt BusQuittEin gesendet, wenn 
								 // Modem im Konfigurations-Modus ist. Danach Sendung der
								 // aktuellen Konfigurationsdaten im Format F(high-nibble) F(low-nibble)
								 // 1B wird also als F1 FB gesendet
#define BusKdoSpezial 		0xE0 // und alle folgenden
*/

// I²C-Parameter
// -------------

#define BusFrequenz 30000

// Hinweise zur Initialisierung
// ----------------------------
// Reset + 0,5 Sek.: I²C initialisieren
// Reset + 0,75 Sek.: I²C nochmal resetten
// Reset + 1 Sek.: früheste Sendung auf I²C 
