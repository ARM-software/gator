# Copyright (C) Arm Limited 2015-2016. All rights reserved.

CROSS_COMPILE_ARM = arm-linux-gnueabihf-
CROSS_COMPILE_AARCH64 = aarch64-linux-gnu-

AS = as
LD = ld
OBJCOPY = objcopy

all: armv7 aarch64

armv7.o: armv7.s
	$(CROSS_COMPILE_ARM)$(AS) -mfpu=vfp -o $@ $^

armv7.elf: armv7.o
	$(CROSS_COMPILE_ARM)$(LD) -Ttext 10000 -o $@ $^

armv7: armv7.elf
	$(CROSS_COMPILE_ARM)$(OBJCOPY) -O binary $^ $@


aarch64.o: aarch64.s
	$(CROSS_COMPILE_AARCH64)$(AS) -o $@ $^

aarch64.elf: aarch64.o
	$(CROSS_COMPILE_AARCH64)$(LD) -Ttext 400000 -o $@ $^

aarch64: aarch64.elf
	$(CROSS_COMPILE_AARCH64)$(OBJCOPY) -O binary $^ $@

.PHONY: clean

clean:
	rm -f *~ *.o *.elf armv7 aarch64
