KOBO_CROSS_COMPILE ?= arm-nickel-linux-gnueabihf-
KOBO_LOCAL_CXX := $(CURDIR)/.toolchains/bin/arm-nickel-linux-gnueabihf-g++
ifneq ($(wildcard $(KOBO_LOCAL_CXX)),)
KOBO_CC ?= $(CURDIR)/scripts/nickeltc-tool arm-nickel-linux-gnueabihf-gcc
KOBO_CXX ?= $(KOBO_LOCAL_CXX)
KOBO_LINK ?= $(CURDIR)/scripts/nickeltc-link
else
KOBO_CC ?= $(KOBO_CROSS_COMPILE)gcc
KOBO_CXX ?= $(KOBO_CROSS_COMPILE)g++
KOBO_LINK ?= $(KOBO_CXX)
endif

KOBO_CPU_FLAGS := -march=armv7-a -mtune=cortex-a53 \
	-mfpu=neon-vfpv4 -mfloat-abi=hard
KOBO_CXXFLAGS := -O3 -std=c++11 -Wall -Wextra -fomit-frame-pointer \
	$(KOBO_CPU_FLAGS)
KOBO_LDFLAGS := -static-libgcc -static-libstdc++
