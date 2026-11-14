.DEFAULT_GOAL := help

include mk/ewdb.mk
include mk/gambatte.mk

# NickelHook defines the legacy all/clean/koboroot targets. Load it only for
# those internal targets so EWDB and help do not require initialized submodules.
ifneq ($(filter all clean koboroot,$(MAKECMDGOALS)),)
include mk/nickelrefresh.mk
endif

.PHONY: help nickelrefresh nickelrefresh-package

help:
	@echo "NickelSperiments build targets:"
	@echo "  nickelrefresh          Build the NickelRefresh plugin"
	@echo "  nickelrefresh-package  Build NickelRefresh KoboRoot.tgz"
	@echo "  ewdb-binary            Build the pinned ARMv7 EWDB daemon"
	@echo "  ewdb-package           Build build/EWDB-KoboRoot.tgz"
	@echo "  gambatte-bench         Build the headless ARMv7 Gambatte benchmark"

nickelrefresh:
	$(MAKE) all

nickelrefresh-package:
	$(MAKE) clean all koboroot
