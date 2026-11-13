include deps/NickelHook/NickelHook.mk

override LIBRARY := libnickelrefresh.so
override SOURCES += src/nickelrefresh.cpp
override CPPFLAGS += -Ideps/FBInk

FBINK_ARCHIVE := deps/FBInk/Debug/libfbink.a
FBINK_MAKE_FLAGS := \
	CROSS_COMPILE=$(CROSS_COMPILE) KOBO=true MINIMAL=true DRAW=1 IMAGE=1 PIC=1 \
	DEBUG=1 'CFLAGS=-O2 -fomit-frame-pointer -pipe' \
	I2C_CPPFLAGS=-Ilibi2c-staged/usr/local/include
I2C_TOOLS_REV := 3a8a0c5515b4890dfb412971511c932f31d93d9c
override LDFLAGS += $(FBINK_ARCHIVE)

# DEBUG avoids FBInk release flags unsupported by GCC 4.9; keep the library optimized.
$(FBINK_ARCHIVE):
	git -C deps/FBInk config submodule.i2c-tools.url https://github.com/mozilla-b2g/i2c-tools.git
	git -C deps/FBInk config submodule.i2c-tools.branch master
	git -C deps/FBInk submodule update --init --remote i2c-tools
	git -C deps/FBInk/i2c-tools checkout --detach $(I2C_TOOLS_REV)
	git -C deps/FBInk submodule update --init stb
	$(MAKE) -C deps/FBInk $(FBINK_MAKE_FLAGS) staticlib

$(LIBRARY): | $(FBINK_ARCHIVE)

include deps/NickelHook/NickelHook.mk
