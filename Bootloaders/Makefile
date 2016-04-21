HEADERS = CHIPKIT_MAX32.h CHIPKIT_UC32.h CHIPKIT_UNO32.h MAJENKO_LENNY.h

all: $(HEADERS)


CHIPKIT_MAX32.h: CHIPKIT_MAX32.hex
	@echo "#ifdef _BOARD_MEGA_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

CHIPKIT_UC32.h: CHIPKIT_UC32.hex
	@echo "#ifdef _BOARD_UC32_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

CHIPKIT_UNO32.h: CHIPKIT_UNO32.hex
	@echo "#ifdef _BOARD_UNO_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

MAJENKO_LENNY.h: MAJENKO_LENNY.hex
	@echo "#ifdef _BOARD_LENNY_" > $@
	@echo "#define GOT_INTERNAL_BOOTLOADER 1" >> $@
	@echo "const char *bootloaderHex[] = {" >> $@
	@cat $< | awk '{print "    \"" $$1 "\","}' >> $@
	@echo "};" >> $@
	@echo "#endif" >> $@

clean:
	rm -f $(HEADERS)
