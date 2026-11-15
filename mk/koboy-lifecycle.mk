include deps/NickelHook/NickelHook.mk

override LIBRARY := libkoboylifecycle.so
override SOURCES += src/koboy-lifecycle.cpp

ifneq ($(wildcard $(CURDIR)/.toolchains/nickeltc-root),)
LOCAL_NICKELTC := $(CURDIR)/scripts/nickeltc-tool
LOCAL_NICKELTC_SYSROOT := $(CURDIR)/.toolchains/nickeltc-root/tc/arm-nickel-linux-gnueabihf/arm-nickel-linux-gnueabihf/sysroot
override CC := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-gcc
override CXX := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-g++
override MOC := $(LOCAL_NICKELTC) moc
override RCC := $(LOCAL_NICKELTC) rcc
override PKG_CONFIG := env PKG_CONFIG_SYSROOT_DIR=$(LOCAL_NICKELTC_SYSROOT) PKG_CONFIG_LIBDIR=$(LOCAL_NICKELTC_SYSROOT)/usr/lib/pkgconfig pkg-config
override STRIP := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-strip
override AR := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-gcc-ar
override RANLIB := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-gcc-ranlib
override NM := $(LOCAL_NICKELTC) arm-nickel-linux-gnueabihf-gcc-nm
override CROSS_COMPILE := local-nickeltc-
endif

include deps/NickelHook/NickelHook.mk

ifneq ($(wildcard $(CURDIR)/.toolchains/nickeltc-root),)
override nh_cmd_so = $(CURDIR)/scripts/nickeltc-plugin-link -o $(1) $(2)
endif
