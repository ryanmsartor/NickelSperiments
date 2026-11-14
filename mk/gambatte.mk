GAMBATTE_ROOT := deps/gambatte
GAMBATTE_CORE := $(GAMBATTE_ROOT)/libgambatte
GAMBATTE_BUILD_DIR := build/gambatte
GAMBATTE_OUTPUT := build/kobo-gambatte-bench
GAMBATTE_DISPLAY_OUTPUT := build/kobo-gambatte-display-test
GAMBATTE_PLAYER_OUTPUT := build/kobo-gambatte-display
GAMBATTE_SUPERVISOR_OUTPUT := build/kobo-gambatte-supervisor
GAMBATTE_CROSS_COMPILE ?= arm-nickel-linux-gnueabihf-
GAMBATTE_CXX ?= $(GAMBATTE_CROSS_COMPILE)g++

GAMBATTE_CPPFLAGS := \
	-D__LIBRETRO__ \
	-DHAVE_STDINT_H \
	-I$(GAMBATTE_CORE)/include \
	-I$(GAMBATTE_CORE)/src \
	-I$(GAMBATTE_CORE)/libretro \
	-I$(GAMBATTE_CORE)/libretro-common/include \
	-I$(GAMBATTE_ROOT)/common
GAMBATTE_CXXFLAGS := -O3 -std=c++11 -Wall -Wextra -fomit-frame-pointer
GAMBATTE_LDFLAGS := -static-libgcc -static-libstdc++

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

GAMBATTE_CORE_OBJECTS := $(patsubst %.cpp,$(GAMBATTE_BUILD_DIR)/%.o,$(GAMBATTE_CORE_SOURCES))
GAMBATTE_FRONTEND_OBJECTS := \
	$(GAMBATTE_BUILD_DIR)/frontend/gambatte-bench.o \
	$(GAMBATTE_BUILD_DIR)/frontend/gambatte-platform.o
GAMBATTE_DISPLAY_OBJECTS := \
	$(GAMBATTE_BUILD_DIR)/display/display_test.o \
	$(GAMBATTE_BUILD_DIR)/display/frame_scaler.o \
	$(GAMBATTE_BUILD_DIR)/display/framebuffer_backend.o
GAMBATTE_PLAYER_OBJECTS := \
	$(GAMBATTE_BUILD_DIR)/display/gambatte_display.o \
	$(GAMBATTE_BUILD_DIR)/display/frame_scaler.o \
	$(GAMBATTE_BUILD_DIR)/display/framebuffer_backend.o \
	$(GAMBATTE_BUILD_DIR)/display/kobo_session.o \
	$(GAMBATTE_BUILD_DIR)/display/touch_input.o \
	$(GAMBATTE_BUILD_DIR)/frontend/gambatte-platform.o
GAMBATTE_SUPERVISOR_OBJECTS := \
	$(GAMBATTE_BUILD_DIR)/display/supervisor.o \
	$(GAMBATTE_BUILD_DIR)/display/framebuffer_backend.o \
	$(GAMBATTE_BUILD_DIR)/display/kobo_session.o

.PHONY: gambatte-bench gambatte-display-test gambatte-display gambatte-supervisor gambatte-clean

gambatte-bench: $(GAMBATTE_OUTPUT)

gambatte-display-test: $(GAMBATTE_DISPLAY_OUTPUT)

gambatte-display: $(GAMBATTE_PLAYER_OUTPUT)

gambatte-supervisor: $(GAMBATTE_SUPERVISOR_OUTPUT)

$(GAMBATTE_OUTPUT): $(GAMBATTE_CORE_OBJECTS) $(GAMBATTE_FRONTEND_OBJECTS)
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_LDFLAGS) -o $@ $^

$(GAMBATTE_DISPLAY_OUTPUT): $(GAMBATTE_DISPLAY_OBJECTS)
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_LDFLAGS) -o $@ $^

$(GAMBATTE_PLAYER_OUTPUT): $(GAMBATTE_CORE_OBJECTS) $(GAMBATTE_PLAYER_OBJECTS)
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_LDFLAGS) -o $@ $^

$(GAMBATTE_SUPERVISOR_OUTPUT): $(GAMBATTE_SUPERVISOR_OBJECTS)
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_LDFLAGS) -o $@ $^

$(GAMBATTE_BUILD_DIR)/%.o: $(GAMBATTE_ROOT)/%.cpp
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_CPPFLAGS) $(GAMBATTE_CXXFLAGS) -c -o $@ $<

$(GAMBATTE_BUILD_DIR)/frontend/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_CPPFLAGS) $(GAMBATTE_CXXFLAGS) -c -o $@ $<

$(GAMBATTE_BUILD_DIR)/display/%.o: src/gambatte/%.cpp
	@mkdir -p $(@D)
	$(GAMBATTE_CXX) $(GAMBATTE_CPPFLAGS) $(GAMBATTE_CXXFLAGS) -c -o $@ $<

gambatte-clean:
	rm -rf $(GAMBATTE_BUILD_DIR) $(GAMBATTE_OUTPUT) $(GAMBATTE_DISPLAY_OUTPUT) $(GAMBATTE_PLAYER_OUTPUT) $(GAMBATTE_SUPERVISOR_OUTPUT)
