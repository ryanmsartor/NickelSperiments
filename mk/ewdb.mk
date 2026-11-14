.PHONY: ewdb-binary ewdb-package ewdbd ewdb-koboroot

EWDB_PACKAGE_ROOT := build/ewdb-package-root
EWDB_PACKAGE := build/EWDB-KoboRoot.tgz

ewdb-binary:
	./scripts/build-ewdbd.sh

ewdb-package: ewdb-binary
	rm -rf $(EWDB_PACKAGE_ROOT)
	install -d $(EWDB_PACKAGE_ROOT)
	cp -a package/ewdb/. $(EWDB_PACKAGE_ROOT)/
	install -Dm755 build/ewdbd-linux-armv7 \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/ewdbd
	find $(EWDB_PACKAGE_ROOT) -type d -exec chmod 755 {} +
	find $(EWDB_PACKAGE_ROOT) -type f -exec chmod 644 {} +
	chmod 755 $(EWDB_PACKAGE_ROOT)/usr/local/ewdb/ewdbd \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/ewdb-usb \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/usb-event \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/install-storage-hook \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/storage-event \
		$(EWDB_PACKAGE_ROOT)/usr/local/ewdb/uninstall
	tar --owner=0 --group=0 --numeric-owner -C $(EWDB_PACKAGE_ROOT) \
		-czf $(EWDB_PACKAGE) .

# Compatibility aliases retained for existing local commands.
ewdbd: ewdb-binary
ewdb-koboroot: ewdb-package
