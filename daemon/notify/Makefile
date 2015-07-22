ifneq ($(SDKDIR),)

# Find the oldest SDK Platform installed >= 16
SDK_PLATFORM := $(shell ls -d $(SDKDIR)/platforms/android-1[6-9] $(SDKDIR)/platforms/android-[2-9][0-9] | head -1)
# Find the newest SDK Build-tools
SDK_BUILD_TOOLS := $(shell ls -d $(SDKDIR)/build-tools/* | tail -1)

all: notify.dex

notify.dex: Notify.java
	javac -cp $(SDK_PLATFORM)/data/layoutlib.jar -source 1.6 -target 1.6 $^
	$(SDK_BUILD_TOOLS)/dx --dex --output=$@ $(^:%.java=%.class)

else

all:
	$(error Please specify SDKDIR as the location to the Android SDK)

endif

.PHONY: clean

clean:
	rm -f *~ *.class *.dex
