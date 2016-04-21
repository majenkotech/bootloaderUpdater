HEADERS = CHIPKIT_MAX32.h CHIPKIT_UC32.h CHIPKIT_UNO32.h MAJENKO_LENNY.h MECANIQUE_FIREWING.h

all: $(HEADERS)


CHIPKIT_MAX32.h: CHIPKIT_MAX32.hex
	@echo "Generating $@"
	@echo "#ifdef _BOARD_MEGA_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

CHIPKIT_UC32.h: CHIPKIT_UC32.hex
	@echo "Generating $@"
	@echo "#ifdef _BOARD_UC32_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

CHIPKIT_UNO32.h: CHIPKIT_UNO32.hex
	@echo "Generating $@"
	@echo "#ifdef _BOARD_UNO_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

MAJENKO_LENNY.h: MAJENKO_LENNY.hex
	@echo "Generating $@"
	@echo "#ifdef _BOARD_LENNY_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

MECANIQUE_FIREWING.h: MECANIQUE_FIREWING.hex
	@echo "Generating $@"
	@echo "#ifdef _BOARD_MECANIQUE_FIREWING_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

clean:
	rm -f $(HEADERS)
