ARMNES_ROOT := deps/armnes
ARMNES_OBJ_DIR := build/obj/armnes
ARMNES_OUTPUT := build/armnes_libretro.so

ARMNES_CORE_SOURCES := \
	src/apu.c src/boards/asic.c src/boards/bandai.c \
	src/boards/basic.c src/boards/discrete.c src/boards/fds.c \
	src/boards/jy.c src/boards/list.c src/boards/mmc3.c \
	src/boards/mmc5.c src/boards/multicart.c src/boards/nsf.c \
	src/boards/rare.c src/boards/sachen.c src/sound/emu2413.c \
	src/bus.c src/cart.c src/cheat.c src/cpu.c src/input.c src/nes.c \
	src/par.c src/genie.c src/ppu.c src/ppu-fast.c src/state.c src/arcade.c \
	src/fceu/bridge.c shared/libretro/options.c libretro/libretro.c \
	libretro/composite.c
ARMNES_FCEU_SOURCES := $(shell cd $(ARMNES_ROOT) && \
	find src/fceu/boards -name '*.c' | sort)
ARMNES_ARCHIVE_SOURCES := $(shell cd $(ARMNES_ROOT) && \
	find libretro/deps/libretro-common libretro/deps/7zip -name '*.c' | sort)
ARMNES_SOURCES := $(ARMNES_CORE_SOURCES) $(ARMNES_FCEU_SOURCES) \
	$(ARMNES_ARCHIVE_SOURCES)
ARMNES_OBJECTS := $(patsubst %.c,$(ARMNES_OBJ_DIR)/%.o,$(ARMNES_SOURCES))
ARMNES_DEPENDENCIES := $(ARMNES_OBJECTS:.o=.d)

ARMNES_CPPFLAGS := \
	-I$(ARMNES_ROOT)/src -I$(ARMNES_ROOT)/src/boards \
	-I$(ARMNES_ROOT)/src/fceu -I$(ARMNES_ROOT)/src/fceu/boards \
	-I$(ARMNES_ROOT)/libretro/deps/libretro-common/include \
	-I$(ARMNES_ROOT)/shared/libretro \
	-I$(ARMNES_ROOT)/libretro/deps \
	-DHAVE_ZLIB -DHAVE_7ZIP
ARMNES_CFLAGS := -O3 -std=gnu11 -Wall -Wextra -fwrapv -fPIC \
	-fvisibility=hidden $(KOBO_DEPFLAGS) $(KOBO_CPU_FLAGS)

-include $(ARMNES_DEPENDENCIES)

.PHONY: armnes-core armnes-clean

armnes-core: $(ARMNES_OUTPUT)

$(ARMNES_OUTPUT): $(ARMNES_OBJECTS)
	@mkdir -p $(@D)
	./scripts/nickeltc-core-link --version-script \
		$(ARMNES_ROOT)/shared/libretro/link.T -o $@ $^

$(ARMNES_OBJ_DIR)/src/fceu/boards/%.o: $(ARMNES_ROOT)/src/fceu/boards/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMNES_CPPFLAGS) $(ARMNES_CFLAGS) -w -c -o $@ $<

$(ARMNES_OBJ_DIR)/%.o: $(ARMNES_ROOT)/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(ARMNES_CPPFLAGS) $(ARMNES_CFLAGS) -c -o $@ $<

armnes-clean:
	rm -rf $(ARMNES_OBJ_DIR) $(ARMNES_OUTPUT)
