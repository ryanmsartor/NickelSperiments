ARMGBA_ROOT := deps/armgba
ARMGBA_OBJ_DIR := build/obj/armgba
ARMGBA_ADAPTER_CHECK_OBJECT := $(ARMGBA_OBJ_DIR)/koboy/armgba_core.o

ARMGBA_CORE_SOURCES := \
	src/bios-image.c src/bus.c src/cheats.c src/gba.c src/memory.c \
	src/state.c src/timing.c src/audio/audio.c src/audio/psg.c \
	src/cart/actionreplay.c src/cart/fcd.c src/cart/cart.c \
	src/cart/ereader.c src/cart/gpio.c src/cart/overrides.c \
	src/cart/savedata.c src/cpu/arm.c src/cpu/decode.c src/cpu/step.c \
	src/io/dma.c src/io/io.c src/io/rfu.c src/io/sio.c src/io/timer.c \
	src/video/render-thread.c src/video/render.c src/video/render-neon.c \
	src/video/video.c src/cpu/jit.c src/cpu/a32/backend.c
ARMGBA_CORE_OBJECTS := $(patsubst %.c,$(ARMGBA_OBJ_DIR)/%.o,$(ARMGBA_CORE_SOURCES))
ARMGBA_LIBRETRO_C_SOURCES := \
	libretro/libretro.c libretro/clock.c \
	libretro/options.c libretro/saves.c libretro/dolphin.c \
	libretro/link.c libretro/games.c libretro/cht.c \
	libretro/gbacheats.c libretro/savefiles.c libretro/patch.c \
	libretro/dmg07.c libretro/ereader.c libretro/printer.c \
	libretro/printout.c libretro/workers.c \
	libretro/megaduck/laptop.c libretro/megaduck/megaduck_laptop.c \
	libretro/megaduck/megaduck_laptop_init.c \
	libretro/megaduck/megaduck_laptop_keyboard.c \
	libretro/megaduck/megaduck_laptop_printer.c \
	libretro/megaduck/megaduck_laptop_rtc.c \
	libretro/megaduck/megaduck_laptop_speech.c \
	libretro/megaduck/megaduck_laptop_buffer_receive.c \
	libretro/megaduck/megaduck_laptop_buffer_send.c
ARMGBA_LIBRETRO_C_OBJECTS := \
	$(patsubst %.c,$(ARMGBA_OBJ_DIR)/%.o,$(ARMGBA_LIBRETRO_C_SOURCES))
# NickelTC GCC 4.9 cannot compile libretro/lcd.c's optional CPU LCD effects:
# it predates __builtin_convertvector. Ko Boy does its own display processing,
# so the eventual link will provide a no-effects compatibility implementation.
ARMGBA_LIBRETRO_BLOCKED_SOURCE := libretro/lcd.c
ARMGBA_LCD_COMPAT_OBJECT := $(ARMGBA_OBJ_DIR)/koboy/armgba_lcd_compat.o
ARMGBA_GB_ADAPTER_OBJECT := $(ARMGBA_OBJ_DIR)/libretro/gb-gambatte.o
ARMGBA_GAMBATTE_SOURCES := $(shell cd $(ARMGBA_ROOT) && \
	find gambatte/libgambatte/src -name '*.cpp' \
	! -name 'file_zip.cpp' ! -name 'cinterface.cpp' ! -path '*/unzip/*' | sort)
ARMGBA_GAMBATTE_OBJECTS := \
	$(patsubst %.cpp,$(ARMGBA_OBJ_DIR)/%.o,$(ARMGBA_GAMBATTE_SOURCES))
ARMGBA_ARCHIVE_SOURCES := $(shell cd $(ARMGBA_ROOT) && \
	find libretro/deps/libretro-common libretro/deps/7zip -name '*.c' | sort)
ARMGBA_ARCHIVE_OBJECTS := \
	$(patsubst %.c,$(ARMGBA_OBJ_DIR)/%.o,$(ARMGBA_ARCHIVE_SOURCES))
ARMGBA_PROBE_OBJECT := $(ARMGBA_OBJ_DIR)/koboy/armgba_probe.o
ARMGBA_PROBE_OUTPUT := build/armgba-probe

ARMGBA_CPPFLAGS := \
	-I$(ARMGBA_ROOT)/libretro \
	-I$(ARMGBA_ROOT)/src \
	-Isrc/koboy
ARMGBA_CORE_CPPFLAGS := \
	-I$(ARMGBA_ROOT)/libretro \
	-I$(ARMGBA_ROOT)/libretro/deps \
	-I$(ARMGBA_ROOT)/libretro/deps/libretro-common/include \
	-I$(ARMGBA_ROOT)/src
ARMGBA_CFLAGS := -O3 -std=gnu11 -Wall -Wextra -fwrapv \
	-march=armv7-a -mfpu=neon -mfloat-abi=hard
ARMGBA_GAMBATTE_CPPFLAGS := \
	-I$(ARMGBA_ROOT)/gambatte/libgambatte/include \
	-I$(ARMGBA_ROOT)/gambatte/libgambatte/src \
	-I$(ARMGBA_ROOT)/gambatte/common \
	-DHAVE_STDINT_H
ARMGBA_ARCHIVE_CPPFLAGS := \
	-I$(ARMGBA_ROOT)/libretro/deps \
	-I$(ARMGBA_ROOT)/libretro/deps/libretro-common/include \
	-DHAVE_ZLIB -DHAVE_7ZIP

.PHONY: armgba-core-check armgba-libretro-check armgba-adapter-check armgba-probe armgba-clean

armgba-core-check: $(ARMGBA_CORE_OBJECTS)

armgba-libretro-check: $(ARMGBA_LIBRETRO_C_OBJECTS)

armgba-probe: $(ARMGBA_PROBE_OUTPUT)

$(ARMGBA_PROBE_OUTPUT): $(ARMGBA_CORE_OBJECTS) $(ARMGBA_LIBRETRO_C_OBJECTS) \
		$(ARMGBA_LCD_COMPAT_OBJECT) $(ARMGBA_GB_ADAPTER_OBJECT) \
		$(ARMGBA_GAMBATTE_OBJECTS) $(ARMGBA_ARCHIVE_OBJECTS) \
		$(ARMGBA_ADAPTER_CHECK_OBJECT) $(ARMGBA_PROBE_OBJECT)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^ -lpthread -lz

armgba-adapter-check: $(ARMGBA_ADAPTER_CHECK_OBJECT)

$(ARMGBA_ADAPTER_CHECK_OBJECT): src/koboy/armgba_core.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(ARMGBA_PROBE_OBJECT): src/koboy/armgba_probe.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(ARMGBA_LCD_COMPAT_OBJECT): src/koboy/armgba_lcd_compat.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMGBA_CORE_CPPFLAGS) $(ARMGBA_CFLAGS) -c -o $@ $<

$(ARMGBA_GB_ADAPTER_OBJECT): $(ARMGBA_ROOT)/libretro/gb-gambatte.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_CORE_CPPFLAGS) $(ARMGBA_GAMBATTE_CPPFLAGS) \
		$(KOBO_CXXFLAGS) -c -o $@ $<

$(ARMGBA_OBJ_DIR)/gambatte/%.o: $(ARMGBA_ROOT)/gambatte/%.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_GAMBATTE_CPPFLAGS) $(KOBO_CXXFLAGS) \
		-fno-exceptions -fno-rtti -c -o $@ $<

$(ARMGBA_OBJ_DIR)/libretro/deps/%.o: $(ARMGBA_ROOT)/libretro/deps/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMGBA_ARCHIVE_CPPFLAGS) $(ARMGBA_CFLAGS) -c -o $@ $<

$(ARMGBA_OBJ_DIR)/%.o: $(ARMGBA_ROOT)/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMGBA_CORE_CPPFLAGS) $(ARMGBA_CFLAGS) -c -o $@ $<

armgba-clean:
	rm -rf $(ARMGBA_OBJ_DIR) $(ARMGBA_PROBE_OUTPUT)
