#include <sys/kmem.h>
#include <CLI.h>

#include "Bootloaders/CHIPKIT_MAX32.h"
#include "Bootloaders/CHIPKIT_UC32.h"
#include "Bootloaders/CHIPKIT_UNO32.h"
#include "Bootloaders/MAJENKO_LENNY.h"
#include "Bootloaders/MECANIQUE_FIREWING.h"

#ifdef GOT_INTERNAL_BOOTLOADER
#define BOOTLOADER_LEN (sizeof(bootloaderHex) / sizeof(bootloaderHex[0]))
#endif

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

uint32_t UUID = 0;

struct page *allocateNewPage(Stream *dev, uint32_t address) {
	pageCount++;
	dev->printf("Loading page 0x%08x\r\n", address);
	struct page *p = (struct page *)malloc(sizeof(struct page));

	if (p == NULL) {
		dev->println("Unable to allocate memory!");
	    return NULL;
	}

	p->startAddress = address;
	p->next = NULL;

	for (int i = 0; i < _EEPROM_PAGE_SIZE; i++) {
		p->data[i] = 0xFFFFFFFFUL;
	}

	return p;
}

struct page *fetchPage(Stream *dev, uint32_t address) {
	address = KVA_TO_PA(address);
	uint32_t pageAddress = address & ~((_EEPROM_PAGE_SIZE * 4) - 1);

	if (bootloader == NULL) {
		bootloader = allocateNewPage(dev, pageAddress);
		return bootloader;
	}

	struct page *scan;

	for (scan = bootloader; scan; scan = scan->next) {
		if (scan->startAddress == pageAddress) {
			return scan;
		}
	}

	struct page *newPage = allocateNewPage(dev, pageAddress);

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

int parseHex(Stream *dev, const char *line) {
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
			p = fetchPage(dev, fullAddress);
            if (p == NULL) {
                dev->println("Error loading page");
                return 20;
            }
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

static inline char cleanChar(char c) {
    if (c < ' ') return '.';
    if (c > 126) return '.';
    return c;
}

void dumpPage(Stream *dev, struct page *p) {
	dev->println();
	dev->printf("Page start 0x%02X\r\n", p->startAddress);

	for (int i = 0; i < _EEPROM_PAGE_SIZE; i += 4) {
		dev->printf("%04x: %08X %08X %08X %08X %c%c%c%c %c%c%c%c %c%c%c%c %c%c%c%c\r\n", i*4, p->data[i], p->data[i + 1], p->data[i + 2], p->data[i + 3],
		    cleanChar(p->data[i] >> 24), cleanChar(p->data[i] >> 16), cleanChar(p->data[i] >> 8), cleanChar(p->data[i]),
            cleanChar(p->data[i+1] >> 24), cleanChar(p->data[i+1] >> 16), cleanChar(p->data[i+1] >> 8), cleanChar(p->data[i+1]),
            cleanChar(p->data[i+2] >> 24), cleanChar(p->data[i+2] >> 16), cleanChar(p->data[i+2] >> 8), cleanChar(p->data[i+2]),
            cleanChar(p->data[i+3] >> 24), cleanChar(p->data[i+3] >> 16), cleanChar(p->data[i+3] >> 8), cleanChar(p->data[i+3])
		);
	}
}

int blockingRead(Stream *dev) {
	while (!dev->available());

	return dev->read();
}

bool loadInternalBootloader(Stream *dev) {
#ifdef GOT_INTERNAL_BOOTLOADER
	currentOffset = 0;
	pageCount = 0;

	for (int i = 0; i < BOOTLOADER_LEN; i++) {
		int ret = parseHex(dev, bootloaderHex[i]);

		if (ret == PROC_CSUM) {
			dev->print("Checksum error at line ");
			dev->println(i);
			return false;
		}
	}

	return true;
#else
	dev->println("Sorry, there is no internal bootloader for your");
	dev->println("board. You will have to manually upload one using");
	dev->println("the 'load ascii' command.");
	return false;
#endif
}

bool loadExternalBootloader(Stream *dev) {
	char line[80];
	int pos = 0;
	currentOffset = 0;
	pageCount = 0;
	int ret = 0;
	int lineno = 0;
	dev->println("Send HEX file data now (Send ASCII)");
	dev->println("Press 'x' to abort");

	while (1) {
		char c = blockingRead(dev);

		switch (c) {
			case 'x':
				return false;

			case '\r':
				break;

			case '\n':
				lineno++;
				ret = parseHex(dev, line);

				if (ret == PROC_CSUM) {
					dev->print("Checksum error at line ");
					dev->println(lineno);
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

void clearOldBootloader() {
    if (bootloader == NULL) {
        return;
    }

    struct page *scan = bootloader;
    while (scan) {
        struct page *next = scan->next;
        free(scan);
        scan = next;
    }
    bootloader = NULL;
    pageCount = 0;
    
}

CLI_COMMAND(help) {
    dev->println();
    dev->println();
    dev->println("Bootloader Update System");
    dev->println("(c) 2016 Majenko Technologies");
    dev->println();
    dev->println("This software comes with no warranty whatsoever. Use of it");
    dev->println("is entirely at the user's own risk. Majenko Technologies,");
    dev->println("chipKIT, nor any of its associates, members or partners,");
    dev->println("can be held responsible for problems arising from the use");
    dev->println("of this software.");
    dev->println();
    dev->println("Commands:");
    dev->println("  help              This screen.");
    dev->println("  load <source>     Load bootloader from <source>");
    dev->println("       internal     Load internally bundled bootloader");
    dev->println("       ascii        Receive a plain text HEX file as ASCII");
    dev->println("  info              Display current state information");
    dev->println("  burn              Burn the loaded bootloader");
    dev->println("  dump              Dump the bootloader to the screen");
    dev->println("  userid <uid>      Set the board's UUID");
    dev->println("  reboot            Reboot the board");
    return 0;
}

CLI_COMMAND(dump) {
    if (bootloader == NULL) {
        dev->println("No bootloader loaded. Use the 'load' command first.");
        return 10;
    }

    for (struct page *scan = bootloader; scan; scan = scan->next) {
        dumpPage(dev, scan);
    }
    return 0;
}

CLI_COMMAND(userid) {
    if (argc != 2) {
        dev->println("Usage: 'userid <uid>' where '<uid>' id 4 hexadecimal digits 0000 to FFFF");
        return 10;
    }
    if (strlen(argv[1]) != 4) {
        dev->println("Usage: 'userid <uid>' where '<uid>' id 4 hexadecimal digits 0000 to FFFF");
        return 10;        
    }
    UUID = h2d4be(argv[1][0], argv[1][1], argv[1][2], argv[1][3]);
    dev->print("User ID:              0x");
    dev->println(UUID, HEX);
    return 0;    
}

CLI_COMMAND(load) {
    if (argc != 2) {
        dev->println("Usage: 'load <source>' where <source> is 'internal' or 'ascii'");
        return 10;
    }
    if (!strcmp(argv[1], "internal")) {
        clearOldBootloader();
        if (!loadInternalBootloader(dev)) {
            return 20;
        }
        return 0;
    }
    if (!strcmp(argv[1], "ascii")) {
        clearOldBootloader();
        if (!loadExternalBootloader(dev)) {
            return 20;
        }
        return 0;
    }
    dev->println("Usage: load <source> where <source> is 'internal' or 'ascii'");
    return 10;
}

CLI_COMMAND(info) {
    dev->print("Bootloader loaded:    ");
    if (bootloader == NULL) {
        dev->println("Nothing loaded");
    } else {
        dev->print(pageCount);
        dev->println(" pages loaded");
    }
    dev->print("User ID:              0x");
    dev->println(UUID, HEX);
    return 0;
}

extern "C" void _softwareReset();

CLI_COMMAND(reboot) {
    executeSoftReset(0);
    return 0;
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
        #ifdef _USE_USB_FOR_SERIAL_
        Serial0.end();
        Serial0.begin(9600 * (F_CPU / 4000000));
        #endif
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

CLI_COMMAND(burn) {
    if (bootloader == NULL) {
        dev->println("No bootloader loaded. Use the 'load' command first.");
        return 10;
    }
    dev->println("Burning now...");

    struct page *p = fetchPage(dev, (uint32_t)&DEVCFG0);
    if (p == NULL) {
        dev->println("Unable to load configuration page");
        return 10;
    }
    p->flags |= PAGE_FRC;

    p->data[ADD_TO_PO(&DEVCFG1)] &= 0xFFFF3FFF; // Force clock switching on

    p->data[ADD_TO_PO(&DEVCFG3)] &= 0xFFFF0000; // Blank the User ID
    p->data[ADD_TO_PO(&DEVCFG3)] |= (UUID & 0xFFFF); // Replace with existing one

    for (struct page *scan = bootloader; scan; scan = scan->next) {
        if (scan->flags & PAGE_RO) {
            dev->print("Skipping 0x");
            dev->println(scan->startAddress, HEX);
            dev->flush();
            continue;
        }
        if (scan->flags & PAGE_FRC) {
            selectClock(true);
        }
        dev->print("Programming 0x");
        dev->println(scan->startAddress, HEX);
        dev->flush();

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
}

void setup() {

    UUID = (DEVCFG3 & 0xFFFF);
    
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, LOW);
    CLI.setDefaultPrompt(_BOARD_NAME_ "> ");

    Serial.begin(9600);
    CLI.addClient(Serial);

#ifdef _USE_USB_FOR_SERIAL_
    Serial0.begin(9600);
    help(CLI.addClient(Serial0), 0, NULL);
#endif
    
    CLI.addCommand("help", help);
    CLI.addCommand("load", load);
    CLI.addCommand("info", info);
    CLI.addCommand("reboot", reboot);
    CLI.addCommand("burn", burn);
    CLI.addCommand("userid", userid);
    CLI.addCommand("dump", dump);
}

void loop() {
    CLI.process();
}
