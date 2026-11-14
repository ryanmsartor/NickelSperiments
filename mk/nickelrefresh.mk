include deps/NickelHook/NickelHook.mk

override LIBRARY := libnickelrefresh.so
override SOURCES += src/nickelrefresh.cpp
override CPPFLAGS += -Ideps/FBInk

FBINK_ARCHIVE := deps/FBInk/Debug/libfbink.a
FBINK_MAKE_FLAGS := \
	CROSS_COMPILE=$(CROSS_COMPILE) KOBO=true MINIMAL=true DRAW=1 IMAGE=1 PIC=1 \
	DEBUG=1 'CFLAGS=-O2 -fomit-frame-pointer -pipe'
I2C_TOOLS_URL := https://github.com/kernelorg-mirror/utils_i2c-tools_i2c-tools.git
I2C_TOOLS_REV := ec1267371e378fc6aaf1cd7686b5b9b31293255a
override LDFLAGS += $(FBINK_ARCHIVE)

# DEBUG avoids FBInk release flags unsupported by GCC 4.9; keep the library optimized.
$(FBINK_ARCHIVE):
	if test -e deps/FBInk/i2c-tools/.git; then \
		git -C deps/FBInk/i2c-tools remote set-url origin $(I2C_TOOLS_URL); \
		if git -C deps/FBInk/i2c-tools rev-parse --is-shallow-repository | grep -q true; then \
			git -C deps/FBInk/i2c-tools fetch --unshallow origin master; \
		else \
			git -C deps/FBInk/i2c-tools fetch origin master; \
		fi; \
	else \
		git clone --branch master $(I2C_TOOLS_URL) deps/FBInk/i2c-tools; \
	fi
	git -C deps/FBInk/i2c-tools checkout --detach $(I2C_TOOLS_REV)
	git -C deps/FBInk submodule update --init stb
	$(MAKE) -C deps/FBInk $(FBINK_MAKE_FLAGS) staticlib

$(LIBRARY): | $(FBINK_ARCHIVE)

include deps/NickelHook/NickelHook.mk
