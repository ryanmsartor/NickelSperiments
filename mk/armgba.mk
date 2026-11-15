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

ARMGBA_CPPFLAGS := \
	-I$(ARMGBA_ROOT)/libretro \
	-I$(ARMGBA_ROOT)/src \
	-Isrc/koboy
ARMGBA_CFLAGS := -O3 -std=gnu11 -Wall -Wextra -fwrapv \
	-march=armv7-a -mfpu=neon -mfloat-abi=hard

.PHONY: armgba-core-check armgba-adapter-check armgba-clean

armgba-core-check: $(ARMGBA_CORE_OBJECTS)

armgba-adapter-check: $(ARMGBA_ADAPTER_CHECK_OBJECT)

$(ARMGBA_ADAPTER_CHECK_OBJECT): src/koboy/armgba_core.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(ARMGBA_OBJ_DIR)/%.o: $(ARMGBA_ROOT)/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) -I$(ARMGBA_ROOT)/src $(ARMGBA_CFLAGS) -c -o $@ $<

armgba-clean:
	rm -rf $(ARMGBA_OBJ_DIR)
