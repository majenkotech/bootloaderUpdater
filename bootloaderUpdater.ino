#include <sys/kmem.h>

#include "Bootloaders/CHIPKIT_MAX32.h"
#include "Bootloaders/CHIPKIT_UC32.h"
#include "Bootloaders/CHIPKIT_UNO32.h"
#include "Bootloaders/MAJENKO_LENNY.h"
#include "Bootloaders/MECANIQUE_FIREWING.h"

#ifdef GOT_INTERNAL_BOOTLOADER
#define BOOTLOADER_LEN (sizeof(bootloaderHex) / sizeof(bootloaderHex[0]))
#endif

//#if defined(__PIC32MX1XX__) || defined(__PIC32MX2XX__)
//#error Sorry, there is a problem writing to boot flash on the PIC32MX1xx/2xx chips.
//#endif

static const uint32_t PROC_OK = 0;
static const uint32_t PROC_END = 1;
static const uint32_t PROC_CSUM = 2;
static const uint32_t PROC_IGNORE = 3;

static const uint32_t nvmopNop          = 0x4000;
static const uint32_t nvmopWriteWord    = 0x4001;
static const uint32_t nvmopWriteRow     = 0x4003;
static const uint32_t nvmopErasePage    = 0x4004;

static const uint32_t PAGE_RO           = 0x0001;
static const uint32_t PAGE_FRC          = 0x0002;

// Convert an address into a page offset 
#define ADD_TO_PO(X) (((uint32_t)((X))>>2) & (_EEPROM_PAGE_SIZE-1))

struct page {
	uint32_t startAddress;
	struct page *next;
	uint32_t flags;
	uint32_t data[_EEPROM_PAGE_SIZE];
};

struct page *bootloader = NULL;
uint32_t currentOffset = 0;
uint32_t pageCount = 0;

struct page *allocateNewPage(uint32_t address) {
	pageCount++;
	Serial.printf("Loading page 0x%08x\r\n", address);
	struct page *p = (struct page *)malloc(sizeof(struct page));

	if (p == NULL) {
		Serial.println("Unable to allocate memory!");

		while (1);
	}

	p->startAddress = address;
	p->next = NULL;

	for (int i = 0; i < _EEPROM_PAGE_SIZE; i++) {
		p->data[i] = 0xFFFFFFFFUL;
	}

	return p;
}

struct page *fetchPage(uint32_t address, bool allocate = true) {
	address = KVA_TO_PA(address);
	uint32_t pageAddress = address & ~((_EEPROM_PAGE_SIZE * 4) - 1);

	if (bootloader == NULL) {
		if (allocate) {
			bootloader = allocateNewPage(pageAddress);
		}

		return bootloader;
	}

	struct page *scan;

	for (scan = bootloader; scan; scan = scan->next) {
		if (scan->startAddress == pageAddress) {
			return scan;
		}
	}

	if (!allocate) {
		return NULL;
	}

	struct page *newPage = allocateNewPage(pageAddress);

	for (scan = bootloader; scan->next; scan = scan->next);

	scan->next = newPage;
	return newPage;
}

bool __attribute__((nomips16)) doNvmOp(uint32_t nvmop) {
	int         intSt;
	uint32_t    tm;

    intSt = disableInterrupts();


    digitalWrite(PIN_LED1, HIGH);

	NVMCON = NVMCON_WREN | nvmop;
	tm = _CP0_GET_COUNT();

	while (_CP0_GET_COUNT() - tm < ((F_CPU * 6) / 2000000));

	NVMKEY      = 0xAA996655;
	NVMKEY      = 0x556699AA;
	NVMCONSET   = NVMCON_WR;

	while (NVMCON & NVMCON_WR) {
		continue;
	}

	NVMCONCLR = NVMCON_WREN;

    digitalWrite(PIN_LED1, LOW);

    restoreInterrupts(intSt);
	
	return (NVMCON & (_NVMCON_WRERR_MASK | _NVMCON_LVDERR_MASK)) ? true : false;
}



static inline uint8_t h2d(uint8_t hex) {
	if (hex > 0x39) {
		hex -= 7;    // adjust for hex letters upper or lower case
	}

	return (hex & 0xf);
}

static inline uint8_t h2d2(uint8_t h1, uint8_t h2) {
	return (h2d(h1) << 4) | h2d(h2);
}

static inline uint16_t h2d4be(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4) {
	return (h2d(h1) << 12) | (h2d(h2) << 8) | (h2d(h3) << 4) | h2d(h4);
}

static inline uint16_t h2d4le(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4) {
	return (h2d(h3) << 12) | (h2d(h4) << 8) | (h2d(h1) << 4) | h2d(h2);
}

static inline uint32_t h2d8le(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4, uint8_t h5, uint8_t h6, uint8_t h7, uint8_t h8) {
	return (h2d(h7) << 28) | (h2d(h8) << 24) | (h2d(h5) << 20) | (h2d(h6) << 16) | (h2d(h3) << 12) | (h2d(h4) << 8) | (h2d(h1) << 4) | h2d(h2);
}

int parseHex(const char *line) {
	if (strlen(line) == 0) {
		return PROC_IGNORE;
	}

	if (line[0] != ':') {
		return PROC_IGNORE;
	}

	uint8_t recordLength = h2d2(line[1], line[2]);
	uint16_t recordAddress = h2d4be(line[3], line[4], line[5], line[6]);
	uint8_t recordType = h2d2(line[7], line[8]);
	uint8_t csum = 0;
	uint32_t fullAddress;
	uint32_t pageOffset;

	for (int i = 1; i < strlen(line) - 2; i += 2) {
		csum += h2d2(line[i], line[i + 1]);
	}

	csum = 0x100 - csum;
	uint8_t exp = h2d2(line[9 + recordLength * 2], line[10 + recordLength * 2]);

	if (csum != exp) {
		return PROC_CSUM;
	}

	struct page *p;

	switch (recordType) {
		case 0: // Data
			fullAddress = currentOffset + recordAddress;
			p = fetchPage(fullAddress);
			pageOffset = (fullAddress >> 2) & (_EEPROM_PAGE_SIZE - 1);

			for (int x = 0; x < recordLength * 2; x += 8) {
				p->data[pageOffset + (x / 8)] = h2d8le(line[9 + x], line[10 + x], line[11 + x], line[12 + x], line[13 + x], line[14 + x], line[15 + x], line[16 + x]);
			}

			break;

		case 1: // End of file
			return PROC_END;
			break;

		case 4: // Offset
			currentOffset = h2d4be(line[9], line[10], line[11], line[12]) << 16;
			break;
	}

	return PROC_OK;
}

void dumpPage(struct page *p) {
	Serial.println();
	Serial.printf("Page start 0x%02X\r\n", p->startAddress);

	for (int i = 0; i < _EEPROM_PAGE_SIZE; i += 4) {
		Serial.printf("%04x: %08X %08X %08X %08X\r\n", i, p->data[i], p->data[i + 1], p->data[i + 2], p->data[i + 3]);
	}
}

int blockingRead() {
	while (!Serial.available());

	return Serial.read();
}

bool loadInternalBootloader() {
#ifdef GOT_INTERNAL_BOOTLOADER
	currentOffset = 0;
	pageCount = 0;

	for (int i = 0; i < BOOTLOADER_LEN; i++) {
		int ret = parseHex(bootloaderHex[i]);

		if (ret == PROC_CSUM) {
			Serial.print("Checksum error at line ");
			Serial.println(i);
			return false;
		}
	}

	return true;
#else
	Serial.println("Sorry, there is no internal bootloader for your board.");
	Serial.println("You will have to manually upload one.");
	return false;
#endif
}

bool loadExternalBootloader() {
	char line[80];
	int pos = 0;
	currentOffset = 0;
	pageCount = 0;
	int ret = 0;
	int lineno = 0;
	Serial.println("Send HEX file data now (Send ASCII)");
	Serial.println("Press 'x' to abort");

	while (1) {
		char c = blockingRead();

		switch (c) {
			case 'x':
				return false;

			case '\r':
				break;

			case '\n':
				lineno++;
				ret = parseHex(line);

				if (ret == PROC_CSUM) {
					Serial.print("Checksum error at line ");
					Serial.println(lineno);
					return false;
				}

				if (ret == PROC_END) {
					return true;
				}

				pos = 0;
				line[0] = 0;
				break;

			default:
				if (pos < 78) {
					line[pos++] = c;
					line[pos] = 0;
				}

				break;
		}
	}
}

void splash() {
	Serial.println();
	Serial.println();
	Serial.println("Bootloader Update System");
	Serial.println("(c) 2016 Majenko Technologies");
	Serial.println();
	Serial.println("This software comes with no warranty whatsoever. Use of it");
	Serial.println("is entirely at the user's own risk. Majenko Technologies,");
	Serial.println("chipKIT, nor any of its associates, members or partners,");
	Serial.println("can be held responsible for problems arising from the use");
	Serial.println("of this software.");
	Serial.println();
	Serial.println("Press I to use the internal bootloader, or U to upload a");
	Serial.println("HEX file from your hard drive.");
}

void selectClock(bool internal) {    
#if defined(__PIC32MX1XX__) || defined(__PIC32MX2XX__)
    static uint32_t osc = OSCCONbits.COSC;


    if (internal) {
        delay(100);
        int f = disableInterrupts();

        SYSKEY = 0x0;
        SYSKEY = 0xAA996655;
        SYSKEY = 0x556699AA;
        OSCCONbits.NOSC = 0b111; // FRC + DIV2
        OSCCONbits.OSWEN = 1;
        SYSKEY = 0x33333333;
        restoreInterrupts(f);
        while (OSCCONbits.OSWEN == 1);
        // Change the baud rate calculation for the new speed.
        Serial.end();
        Serial.begin(9600 * (F_CPU / 4000000));
    } else {
        int f = disableInterrupts();
        SYSKEY = 0x0;
        SYSKEY = 0xAA996655;
        SYSKEY = 0x556699AA;
        OSCCONbits.NOSC = osc; // FRC
        OSCCONbits.OSWEN = 1;
        SYSKEY = 0x33333333;
        restoreInterrupts(f);
        while (OSCCONbits.OSWEN == 1);
        delay(100);
    }
#endif
}

void setup() {

    
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
	Serial.begin(9600);
    
	splash();
	bool ret = false;

	while (ret == false) {
		int ch = blockingRead();

		while (ch != 'i' && ch != 'u') {
			splash();
			ch = blockingRead();
		}

		if (ch == 'i') {
			ret = loadInternalBootloader();
		} else {
			ret = loadExternalBootloader();
		}

		if (!ret) {
			Serial.println("*** Error loading bootloader!");
		}
	}

	//Serial.println("Write protecting config bits...");
	//Serial.flush();
	struct page *p = fetchPage((uint32_t)&DEVCFG0);
	p->flags |= PAGE_FRC;

    p->data[ADD_TO_PO(&DEVCFG1)] &= 0xFFFF3FFF; // Force clock switching on

    p->data[ADD_TO_PO(&DEVCFG3)] &= 0xFFFF0000; // Blank the User ID
    p->data[ADD_TO_PO(&DEVCFG3)] |= (DEVCFG3 & 0xFFFF); // Replace with existing one

   
#ifdef _USE_USB_FOR_SERIAL_
	Serial.println();
    Serial.println("------------------- IMPORTANT ------------------------");
    Serial.println("You are using a direct-USB connected board. The USB");
    Serial.println("communications will break during the burning process.");
    Serial.println("This is normal. After typing 'burn' below the LED on");
    Serial.println("your board will flash a few times before switching off.");
    Serial.println("Once it has stopped flashing the burn process is complete");
    Serial.println("and you can reset the board to test the bootloader.");
#endif
    Serial.println();
	Serial.println("Ready to burn bootloader. Type 'burn' to continue.");

	while (blockingRead() != 'b');

	while (blockingRead() != 'u');

	while (blockingRead() != 'r');

	while (blockingRead() != 'n');

	Serial.println("Burning now...");

	for (struct page *scan = bootloader; scan; scan = scan->next) {
		if (scan->flags & PAGE_RO) {
			Serial.print("Skipping 0x");
			Serial.println(scan->startAddress, HEX);
			Serial.flush();
			continue;
		}
        if (scan->flags & PAGE_FRC) {
            selectClock(true);
        }
		Serial.print("Programming 0x");
		Serial.println(scan->startAddress, HEX);
		Serial.flush();

        NVMADDR = KVA_TO_PA(scan->startAddress);
        doNvmOp(nvmopErasePage);

		for (int i = 0; i < _EEPROM_PAGE_SIZE; i++) {
			NVMADDR = KVA_TO_PA(scan->startAddress + (i * 4));
			NVMDATA = scan->data[i];
			doNvmOp(nvmopWriteWord);
		}
        if (scan->flags & PAGE_FRC) {
//            selectClock(false);
        }
	}

#ifdef _USE_USB_FOR_SERIAL_
    for (int i = 0; i < 10; i++) {
        digitalWrite(PIN_LED1, HIGH);
        delay(100 / (F_CPU / 4000000));
        digitalWrite(PIN_LED1, LOW);
        delay(400 / (F_CPU / 4000000));
    }
    executeSoftReset(ENTER_BOOTLOADER_ON_BOOT);
#else
	Serial.println("All done.");
	Serial.flush();
	Serial.println();
	Serial.println("Press any key to reboot");
	(void)blockingRead();
	executeSoftReset(ENTER_BOOTLOADER_ON_BOOT);
#endif
}

void loop() {
}
