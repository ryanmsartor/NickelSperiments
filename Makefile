.DEFAULT_GOAL := help

include mk/ewdb.mk
include mk/kobo-toolchain.mk
include mk/gambatte.mk
include mk/koboy.mk

# NickelHook defines the legacy all/clean/koboroot targets. Load it only for
# those internal targets so EWDB and help do not require initialized submodules.
ifneq ($(filter all clean koboroot,$(MAKECMDGOALS)),)
include mk/nickelrefresh.mk
endif

.PHONY: help nickelrefresh nickelrefresh-package koboy-lifecycle koboy-lifecycle-package

help:
	@echo "NickelSperiments build targets:"
	@echo "  nickelrefresh          Build the NickelRefresh plugin"
	@echo "  nickelrefresh-package  Build NickelRefresh KoboRoot.tgz"
	@echo "  ewdb-binary            Build the pinned ARMv7 EWDB daemon"
	@echo "  ewdb-package           Build build/EWDB-KoboRoot.tgz"
	@echo "  gambatte-bench         Build the headless ARMv7 Gambatte benchmark"
	@echo "  koboy                  Build the Ko Boy player"
	@echo "  koboy-supervisor       Build the Ko Boy crash-recovery supervisor"
	@echo "  koboy-display-test     Build the Ko Boy synthetic display diagnostic"
	@echo "  koboy-framebuffer-isolation  Build the framebuffer isolation diagnostic"
	@echo "  koboy-lifecycle        Build the Ko Boy Nickel lifecycle bridge"
	@echo "  koboy-lifecycle-package  Package the lifecycle bridge as KoboRoot.tgz"

nickelrefresh:
	$(MAKE) all

nickelrefresh-package:
	$(MAKE) clean all koboroot

koboy-lifecycle:
	$(MAKE) -f mk/koboy-lifecycle.mk all

koboy-lifecycle-package:
	$(MAKE) -f mk/koboy-lifecycle.mk clean all koboroot
