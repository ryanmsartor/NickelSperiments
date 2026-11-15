GAMBATTE_ROOT := deps/gambatte
GAMBATTE_CORE := $(GAMBATTE_ROOT)/libgambatte
GAMBATTE_OBJ_DIR := build/obj/gambatte
GAMBATTE_BENCH_OUTPUT := build/gambatte-bench

GAMBATTE_CPPFLAGS := \
	-D__LIBRETRO__ \
	-DHAVE_STDINT_H \
	-I$(GAMBATTE_CORE)/include \
	-I$(GAMBATTE_CORE)/src \
	-I$(GAMBATTE_CORE)/libretro \
	-I$(GAMBATTE_CORE)/libretro-common/include \
	-I$(GAMBATTE_ROOT)/common

GAMBATTE_CORE_SOURCES := \
	libgambatte/src/bootloader.cpp \
	libgambatte/src/cpu.cpp \
	libgambatte/src/gambatte.cpp \
	libgambatte/src/gambatte-memory.cpp \
	libgambatte/src/initstate.cpp \
	libgambatte/src/interrupter.cpp \
	libgambatte/src/interruptrequester.cpp \
	libgambatte/src/mem/cartridge.cpp \
	libgambatte/src/mem/cartridge_libretro.cpp \
	libgambatte/src/mem/huc3.cpp \
	libgambatte/src/mem/memptrs.cpp \
	libgambatte/src/mem/rtc.cpp \
	libgambatte/src/sound.cpp \
	libgambatte/src/sound/channel1.cpp \
	libgambatte/src/sound/channel2.cpp \
	libgambatte/src/sound/channel3.cpp \
	libgambatte/src/sound/channel4.cpp \
	libgambatte/src/sound/duty_unit.cpp \
	libgambatte/src/sound/envelope_unit.cpp \
	libgambatte/src/sound/length_counter.cpp \
	libgambatte/src/statesaver.cpp \
	libgambatte/src/tima.cpp \
	libgambatte/src/video.cpp \
	libgambatte/src/video/ly_counter.cpp \
	libgambatte/src/video/lyc_irq.cpp \
	libgambatte/src/video/next_m0_time.cpp \
	libgambatte/src/video/ppu.cpp \
	libgambatte/src/video/sprite_mapper.cpp \
	libgambatte/src/video_libretro.cpp

GAMBATTE_CORE_OBJECTS := $(patsubst %.cpp,$(GAMBATTE_OBJ_DIR)/%.o,$(GAMBATTE_CORE_SOURCES))
GAMBATTE_PLATFORM_OBJECT := $(GAMBATTE_OBJ_DIR)/frontend/gambatte-platform.o
GAMBATTE_BENCH_OBJECT := $(GAMBATTE_OBJ_DIR)/frontend/gambatte-bench.o

.PHONY: gambatte-bench gambatte-clean

gambatte-bench: $(GAMBATTE_BENCH_OUTPUT)

$(GAMBATTE_BENCH_OUTPUT): $(GAMBATTE_CORE_OBJECTS) $(GAMBATTE_BENCH_OBJECT) $(GAMBATTE_PLATFORM_OBJECT)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(GAMBATTE_OBJ_DIR)/%.o: $(GAMBATTE_ROOT)/%.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(GAMBATTE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(GAMBATTE_OBJ_DIR)/frontend/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(GAMBATTE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

gambatte-clean:
	rm -rf $(GAMBATTE_OBJ_DIR) $(GAMBATTE_BENCH_OUTPUT)
