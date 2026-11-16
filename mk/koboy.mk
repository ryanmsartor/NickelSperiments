KOBOY_OBJ_DIR := build/obj/koboy
KOBOY_OUTPUT := build/koboy
KOBOY_SUPERVISOR_OUTPUT := build/koboy-supervisor
KOBOY_DISPLAY_TEST_OUTPUT := build/koboy-display-test
KOBOY_FB_ISOLATION_OUTPUT := build/koboy-framebuffer-isolation
KOBOY_PACKAGE_ROOT := build/koboy-package-root
KOBOY_PACKAGE := build/Ko-Boy-KoboRoot.tgz
KOBOY_PACKAGE_CHECKSUM := $(KOBOY_PACKAGE).sha256

KOBOY_COMMON_OBJECTS := \
	$(KOBOY_OBJ_DIR)/frame_scaler.o \
	$(KOBOY_OBJ_DIR)/framebuffer_backend.o
KOBOY_PLAYER_OBJECTS := \
	$(KOBOY_OBJ_DIR)/koboy.o \
	$(KOBOY_OBJ_DIR)/core_factory.o \
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

.PHONY: koboy koboy-supervisor koboy-display-test koboy-framebuffer-isolation koboy-package koboy-clean

koboy: $(KOBOY_OUTPUT)

koboy-supervisor: $(KOBOY_SUPERVISOR_OUTPUT)

koboy-display-test: $(KOBOY_DISPLAY_TEST_OUTPUT)

koboy-framebuffer-isolation: $(KOBOY_FB_ISOLATION_OUTPUT)

koboy-package: koboy koboy-supervisor
	$(MAKE) koboy-lifecycle
	rm -rf $(KOBOY_PACKAGE_ROOT)
	install -d $(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/LICENSES
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy Color"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Game Boy Advance"
	install -d "$(KOBOY_PACKAGE_ROOT)/mnt/onboard/KoBoy/Roms/Mega Duck"
	install -d $(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats
	install -m755 $(KOBOY_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy
	install -m755 $(KOBOY_SUPERVISOR_OUTPUT) \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy-supervisor
	install -m644 libkoboylifecycle.so \
		$(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats/libkoboylifecycle.so
	install -m644 package/koboy/README.txt \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/README.txt
	install -m644 LICENSES/* \
		$(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/LICENSES/
	tar --owner=0 --group=0 --numeric-owner -C $(KOBOY_PACKAGE_ROOT) \
		-czf $(KOBOY_PACKAGE) .
	file $(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy | \
		grep -q 'ELF 32-bit.*ARM'
	file $(KOBOY_PACKAGE_ROOT)/mnt/onboard/.adds/koboy/koboy-supervisor | \
		grep -q 'ELF 32-bit.*ARM'
	file $(KOBOY_PACKAGE_ROOT)/usr/local/Kobo/imageformats/libkoboylifecycle.so | \
		grep -q 'ELF 32-bit.*shared object, ARM'
	test "$$(tar -tzf $(KOBOY_PACKAGE) | grep -c '^./mnt/onboard/.adds/koboy/LICENSES/[^/][^/]*$$')" -eq 6
	cd build && sha256sum $$(basename $(KOBOY_PACKAGE)) > \
		$$(basename $(KOBOY_PACKAGE_CHECKSUM))

$(KOBOY_OUTPUT): $(ARMGBA_PLAYER_OBJECTS) $(KOBOY_PLAYER_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^ -lpthread -lz

$(KOBOY_SUPERVISOR_OUTPUT): $(KOBOY_SUPERVISOR_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_DISPLAY_TEST_OUTPUT): $(KOBOY_DISPLAY_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_FB_ISOLATION_OUTPUT): $(KOBOY_FB_ISOLATION_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

$(KOBOY_OBJ_DIR)/%.o: src/koboy/%.cpp
	@mkdir -p $(@D)
	$(KOBO_CXX) $(KOBO_CXXFLAGS) -c -o $@ $<

koboy-clean:
	rm -rf $(KOBOY_OBJ_DIR) $(KOBOY_OUTPUT) $(KOBOY_SUPERVISOR_OUTPUT) $(KOBOY_DISPLAY_TEST_OUTPUT) $(KOBOY_FB_ISOLATION_OUTPUT) $(KOBOY_PACKAGE_ROOT) $(KOBOY_PACKAGE) $(KOBOY_PACKAGE_CHECKSUM)
