KOBOY_OBJ_DIR := build/obj/koboy
KOBOY_OUTPUT := build/koboy
KOBOY_SUPERVISOR_OUTPUT := build/koboy-supervisor
KOBOY_DISPLAY_TEST_OUTPUT := build/koboy-display-test
KOBOY_FB_ISOLATION_OUTPUT := build/koboy-framebuffer-isolation

KOBOY_COMMON_OBJECTS := \
	$(KOBOY_OBJ_DIR)/frame_scaler.o \
	$(KOBOY_OBJ_DIR)/framebuffer_backend.o
KOBOY_PLAYER_OBJECTS := \
	$(KOBOY_OBJ_DIR)/koboy.o \
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

.PHONY: koboy koboy-supervisor koboy-display-test koboy-framebuffer-isolation koboy-clean

koboy: $(KOBOY_OUTPUT)

koboy-supervisor: $(KOBOY_SUPERVISOR_OUTPUT)

koboy-display-test: $(KOBOY_DISPLAY_TEST_OUTPUT)

koboy-framebuffer-isolation: $(KOBOY_FB_ISOLATION_OUTPUT)

$(KOBOY_OUTPUT): $(GAMBATTE_CORE_OBJECTS) $(GAMBATTE_PLATFORM_OBJECT) $(KOBOY_PLAYER_OBJECTS)
	@mkdir -p $(@D)
	$(KOBO_LINK) $(KOBO_LDFLAGS) -o $@ $^

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
	$(KOBO_CXX) $(GAMBATTE_CPPFLAGS) $(KOBO_CXXFLAGS) -c -o $@ $<

koboy-clean:
	rm -rf $(KOBOY_OBJ_DIR) $(KOBOY_OUTPUT) $(KOBOY_SUPERVISOR_OUTPUT) $(KOBOY_DISPLAY_TEST_OUTPUT) $(KOBOY_FB_ISOLATION_OUTPUT)
