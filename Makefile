.DEFAULT_GOAL := help

include mk/ewdb.mk
include mk/kobo-toolchain.mk
include mk/armgba.mk
include mk/armnes.mk
include mk/koboy.mk

.PHONY: help koboy-lifecycle koboy-lifecycle-package

help:
	@echo "NickelSperiments build targets:"
	@echo "  ewdb-binary            Build the pinned ARMv7 EWDB daemon"
	@echo "  ewdb-package           Build build/EWDB-KoboRoot.tgz"
	@echo "  armgba-adapter-check   Compile the in-progress armGBA frontend adapter"
	@echo "  armgba-core-check      Compile armGBA's ARMv7/NEON/JIT core"
	@echo "  armgba-libretro-check  Compile armGBA's libretro support layer"
	@echo "  armgba-probe           Build the headless armGBA Kobo probe"
	@echo "  armnes-core            Build the pinned ARMv7 armNES libretro core"
	@echo "  armnes-probe           Build the armNES shared-core loader probe"
	@echo "  koboy                  Build the Ko Boy player"
	@echo "  koboy-supervisor       Build the Ko Boy crash-recovery supervisor"
	@echo "  koboy-package          Build build/Ko-Boy-KoboRoot.tgz"
	@echo "  koboy-display-test     Build the Ko Boy synthetic display diagnostic"
	@echo "  koboy-framebuffer-isolation  Build the framebuffer isolation diagnostic"
	@echo "  koboy-lifecycle        Build the Ko Boy Nickel lifecycle bridge"
	@echo "  koboy-lifecycle-package  Package the lifecycle bridge as KoboRoot.tgz"

koboy-lifecycle:
	$(MAKE) -f mk/koboy-lifecycle.mk all

koboy-lifecycle-package:
	$(MAKE) -f mk/koboy-lifecycle.mk clean all koboroot
