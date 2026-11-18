SUPERWHIZKID_ROOT := deps/superwhizkid
SUPERWHIZKID_OBJ_DIR := build/obj/superwhizkid
SUPERWHIZKID_OUTPUT := build/superwhizkid_libretro.so

SUPERWHIZKID_SOURCES := \
	src/gamate/gamate.c src/gamate/cpu.c libretro/gamate.c
SUPERWHIZKID_OBJECTS := $(patsubst %.c,$(SUPERWHIZKID_OBJ_DIR)/%.o,$(SUPERWHIZKID_SOURCES))
SUPERWHIZKID_DEPENDENCIES := $(SUPERWHIZKID_OBJECTS:.o=.d)
SUPERWHIZKID_CPPFLAGS := \
	-I$(SUPERWHIZKID_ROOT)/src -I$(SUPERWHIZKID_ROOT)/shared/libretro
SUPERWHIZKID_CFLAGS := -O3 -std=gnu11 -Wall -Wextra -fwrapv -fPIC \
	-fvisibility=hidden $(KOBO_DEPFLAGS) $(KOBO_CPU_FLAGS)

-include $(SUPERWHIZKID_DEPENDENCIES)

.PHONY: superwhizkid-core superwhizkid-clean

superwhizkid-core: $(SUPERWHIZKID_OUTPUT)

$(SUPERWHIZKID_OUTPUT): $(SUPERWHIZKID_OBJECTS)
	@mkdir -p $(@D)
	./scripts/nickeltc-core-link --version-script \
		$(SUPERWHIZKID_ROOT)/shared/libretro/link.T -o $@ $^

$(SUPERWHIZKID_OBJ_DIR)/%.o: $(SUPERWHIZKID_ROOT)/%.c
	@mkdir -p $(@D)
	$(KOBO_CC) $(SUPERWHIZKID_CPPFLAGS) $(SUPERWHIZKID_CFLAGS) -c -o $@ $<

superwhizkid-clean:
	rm -rf $(SUPERWHIZKID_OBJ_DIR) $(SUPERWHIZKID_OUTPUT)
