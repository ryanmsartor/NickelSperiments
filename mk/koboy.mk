KOBOY_OBJ_DIR := build/obj/koboy
KOBOY_OUTPUT := build/koboy
KOBOY_SUPERVISOR_OUTPUT := build/koboy-supervisor
KOBOY_DISPLAY_TEST_OUTPUT := build/koboy-display-test
KOBOY_FB_ISOLATION_OUTPUT := build/koboy-framebuffer-isolation
KOBOY_PACKAGE_ROOT := build/koboy-package-root
KOBOY_PACKAGE := build/Ko-Boy-KoboRoot.tgz
KOBOY_PACKAGE_CHECKSUM := $(KOBOY_PACKAGE).sha256
KOBOY_LIBRETRO_PROBE_OUTPUT := build/libretro-probe
KOBOY_LIBRETRO_CPPFLAGS := -I$(ARMGBA_ROOT)/libretro -Isrc/koboy

KOBOY_COMMON_OBJECTS := \
	$(KOBOY_OBJ_DIR)/frame_scaler.o \
	$(KOBOY_OBJ_DIR)/framebuffer_backend.o
KOBOY_PLAYER_OBJECTS := \
	$(KOBOY_OBJ_DIR)/koboy.o \
	$(KOBOY_OBJ_DIR)/core_factory.o \
	$(KOBOY_OBJ_DIR)/core_options.o \
	$(KOBOY_OBJ_DIR)/libretro_core.o \
	$(KOBOY_OBJ_DIR)/libretro_library.o \
	$(ARMGBA_ARCHIVE_OBJECTS) \
	$(KOBOY_COMMON_OBJECTS) \
	$(KOBOY_OBJ_DIR)/touch_input.o
KOBOY_SUPERVISOR_OBJECTS := \
	$(KOBOY_OBJ_DIR)/supervisor.o \
	$(KOBOY_OBJ_DIR)/framebuffer_backend.o
KOBOY_DISPLAY_TEST_OBJECTS := \
	$(KOBOY_OBJ_DIR)/display_test.o \
	$(KOBOY_COMMON_OBJECTS)
KOBOY_FB_ISOLATION_OBJECTS := \
	$(KOBOY_OBJ_DIR)/framebuffer_isolation.o \
	$(KOBOY_OBJ_DIR)/framebuffer_backend.o
KOBOY_ALL_OBJECTS := $(sort $(KOBOY_PLAYER_OBJECTS) \
	$(KOBOY_SUPERVISOR_OBJECTS) $(KOBOY_DISPLAY_TEST_OBJECTS) \
	$(KOBOY_FB_ISOLATION_OBJECTS))
KOBOY_DEPENDENCIES := $(KOBOY_ALL_OBJECTS:.o=.d)
KOBOY_LIBRETRO_PROBE_OBJECT := $(KOBOY_OBJ_DIR)/libretro_probe.o
KOBOY_DEPENDENCIES += $(KOBOY_LIBRETRO_PROBE_OBJECT:.o=.d)

-include $(KOBOY_DEPENDENCIES)

.PHONY: koboy koboy-supervisor koboy-display-test koboy-framebuffer-isolation koboy-package koboy-libretro-check libretro-probe koboy-clean

koboy: $(KOBOY_OUTPUT)

koboy-supervisor: $(KOBOY_SUPERVISOR_OUTPUT)

koboy-display-test: $(KOBOY_DISPLAY_TEST_OUTPUT)

koboy-framebuffer-isolation: $(KOBOY_FB_ISOLATION_OUTPUT)

koboy-libretro-check: $(KOBOY_OBJ_DIR)/libretro_core.o

libretro-probe: $(KOBOY_LIBRETRO_PROBE_OUTPUT)

koboy-package: koboy koboy-supervisor armgba-core armnes-core superwhizkid-core
	$(MAKE) koboy-lifecycle
	rm -rf $(KOBOY_PACKAGE_ROOT)
	install -d $(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/LICENSES
	install -d $(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores
	install -d $(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/BIOS
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy Color"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy Advance"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Mega Duck"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Nintendo Entertainment System"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Famicom Disc System"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Gamate"
	install -d $(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats
	install -m755 $(KOBOY_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy
	install -m755 $(KOBOY_SUPERVISOR_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy-supervisor
	install -m755 $(ARMGBA_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/armgba_libretro.so
	install -m755 $(ARMNES_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/armnes_libretro.so
	install -m755 $(SUPERWHIZKID_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/superwhizkid_libretro.so
	install -m644 libkoboylifecycle.so \
		$(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats/libkoboylifecycle.so
	install -m644 package/koboy/README.txt \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/README.txt
	install -m644 LICENSES/KOBOY/KOBOY-LICENSES.md \
		LICENSES/KOBOY/GPL-2.0.txt LICENSES/KOBOY/MPL-2.0.txt \
		LICENSES/KOBOY/MIT.txt LICENSES/KOBOY/EXPAT.txt \
		LICENSES/KOBOY/BSD-3-Clause.txt \
		LICENSES/KOBOY/PUBLIC-DOMAIN-NOTICES.txt \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/LICENSES/
	tar --owner=0 --group=0 --numeric-owner -C $(KOBOY_PACKAGE_ROOT) \
		-czf $(KOBOY_PACKAGE) .
	./scripts/verify-arm-elf executable \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy
	./scripts/verify-arm-elf executable \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy-supervisor
	./scripts/verify-arm-elf shared \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/armgba_libretro.so
	./scripts/verify-arm-elf shared \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/armnes_libretro.so
	./scripts/verify-arm-elf shared \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/cores/superwhizkid_libretro.so
	./scripts/verify-arm-elf shared \
		$(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats/libkoboylifecycle.so
	test "$$(tar -tzf $(KOBOY_PACKAGE) | grep -c '^./mnt/onboard/.adds/koboy/LICENSES/[^/][^/]*$$')" -eq 7
	cd build && sha256sum $$(basename $(KOBOY_PACKAGE)) > \
		$$(basename $(KOBOY_PACKAGE_CHECKSUM))

$(KOBOY_OUTPUT): $(KOBOY_PLAYER_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^ -lpthread -lz -ldl

$(KOBOY_SUPERVISOR_OUTPUT): $(KOBOY_SUPERVISOR_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_DISPLAY_TEST_OUTPUT): $(KOBOY_DISPLAY_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_FB_ISOLATION_OUTPUT): $(KOBOY_FB_ISOLATION_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_OBJ_DIR)/koboy.o: src/koboy/koboy.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_ARCHIVE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_OBJ_DIR)/libretro_library.o: src/koboy/libretro_library.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_ARCHIVE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_OBJ_DIR)/libretro_core.o: src/koboy/libretro_core.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(KOBOY_LIBRETRO_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_LIBRETRO_PROBE_OBJECT): src/koboy/libretro_probe.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(KOBOY_LIBRETRO_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_LIBRETRO_PROBE_OUTPUT): $(KOBOY_LIBRETRO_PROBE_OBJECT) \
	$(KOBOY_OBJ_DIR)/libretro_library.o
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^ -ldl

$(KOBOY_OBJ_DIR)/core_factory.o: src/koboy/core_factory.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(KOBOY_LIBRETRO_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_OBJ_DIR)/core_options.o: src/koboy/core_options.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(ARMGBA_ARCHIVE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

$(KOBOY_OBJ_DIR)/%.o: src/koboy/%.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(KOBO_CXXFLAGS) -c -o $@ $<

koboy-clean:
	rm -rf $(KOBOY_OBJ_DIR) $(KOBOY_OUTPUT) $(KOBOY_SUPERVISOR_OUTPUT) $(KOBOY_DISPLAY_TEST_OUTPUT) $(KOBOY_FB_ISOLATION_OUTPUT) $(KOBOY_LIBRETRO_PROBE_OUTPUT) $(KOBOY_PACKAGE_ROOT) $(KOBOY_PACKAGE) $(KOBOY_PACKAGE_CHECKSUM)
