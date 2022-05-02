#define  SIGNATURE_000 0x1e
#define  SIGNATURE_001 0x97
#define  SIGNATURE_002 0x05
; ***** BOOT_LOAD ********************
#define  BOOTRST 0	// Select Reset Vector
#define  BOOTSZ0 1	// Select Boot Size
#define  BOOTSZ1 2	// Select Boot Size
#define  FLASHEND 0xffff	// Note: Word address
#define  SRAM_START 0x0100
#define  SRAM_SIZE 16384
; ***** BOOTLOADER DECLARATIONS ******************************************
#define  PAGESIZE 128
#define  FIRSTBOOTSTART 0xfe00
#define  SECONDBOOTSTART 0xfc00
#define  THIRDBOOTSTART 0xf800
#define  FOURTHBOOTSTART 0xf000
#define  SMALLBOOTSTART FIRSTBOOTSTART
#define  LARGEBOOTSTART FOURTHBOOTSTART
