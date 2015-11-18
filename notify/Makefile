# Find the oldest SDK Platform installed >= 16
SDK_PLATFORM := $(shell ls -d $(SDKDIR)/platforms/android-1[6-9] $(SDKDIR)/platforms/android-[2-9][0-9] 2> /dev/null | head -1)
# Find layoutlib.jar
LAYOUTLIB ?= $(SDK_PLATFORM)/data/layoutlib.jar
# Find dx from the path or the newest SDK Build-tools
DX := $(shell which dx || ls -d $(SDKDIR)/build-tools/*/dx 2> /dev/null | tail -1)

ifneq ($(LAYOUTLIB),)

all: notify.dex

notify.dex: Notify.java
	javac -cp $(LAYOUTLIB) -source 1.6 -target 1.6 $^
	$(DX) --dex --output=$@ $(^:%.java=%.class)

else

all:
	$(error Please specify SDKDIR as the location to the Android SDK)

endif

.PHONY: clean

clean:
	rm -f *~ *.class *.dex
