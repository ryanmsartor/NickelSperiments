KOBO_CROSS_COMPILE ?= arm-nickel-linux-gnueabihf-
KOBO_LOCAL_CXX := $(CURDIR)/.toolchains/bin/arm-nickel-linux-gnueabihf-g++
ifneq ($(wildcard $(KOBO_LOCAL_CXX)),)
KOBO_CXX ?= $(KOBO_LOCAL_CXX)
KOBO_LINK ?= $(CURDIR)/scripts/nickeltc-link
else
KOBO_CXX ?= $(KOBO_CROSS_COMPILE)g++
KOBO_LINK ?= $(KOBO_CXX)
endif

KOBO_CXXFLAGS := -O3 -std=c++11 -Wall -Wextra -fomit-frame-pointer
KOBO_LDFLAGS := -static-libgcc -static-libstdc++
