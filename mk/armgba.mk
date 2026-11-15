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

.PHONY: armgba-core-check armgba-libretro-check armgba-adapter-check armgba-clean

armgba-core-check: $(ARMGBA_CORE_OBJECTS)

armgba-libretro-check: $(ARMGBA_LIBRETRO_C_OBJECTS)

armgba-adapter-check: $(ARMGBA_ADAPTER_CHECK_OBJECT)

$(ARMGBA_ADAPTER_CHECK_OBJECT): src/koboy/armgba_core.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(ARMGBA_OBJ_DIR)/%.o: $(ARMGBA_ROOT)/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMGBA_CORE_CPPFLAGS) $(ARMGBA_CFLAGS) -c -o $@ $<

armgba-clean:
	rm -rf $(ARMGBA_OBJ_DIR)
