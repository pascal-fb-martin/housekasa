# HouseKasa - A simple home web server for control of TP-Link Kasa devices.
#
# Copyright 2025, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housekasa
HMAN=/var/lib/house/note/content/manuals/automation
HMANCACHE=/var/lib/house/note/cache

# Application build. --------------------------------------------

OBJS= housekasa_device.o housekasa.o
LIBOJS=

all: housekasa kasa

clean:
	rm -f *.o *.a housekasa kasa

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housekasa: $(OBJS)
	gcc -Os -o housekasa $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

kasa: kasa.c
	gcc -Os -o kasa kasa.c

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/kasa
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/kasa
	$(INSTALL) -m 0755 -d $(DESTDIR)$(HMAN)
	$(INSTALL) -m 0644 README.md $(DESTDIR)$(HMAN)/$(HAPP).md
	rm -rf $(DESTDIR)$(HMANCACHE)/*

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housekasa kasa $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housekasa

install-app: install-ui install-runtime

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/housekasa $(DESTDIR)$(prefix)/bin/kasa
	rm -rf $(DESTDIR)$(SHARE)/public/kasa
	rm -f $(DESTDIR)$(HMAN)/$(HAPP).md
	rm -rf $(DESTDIR)$(HMANCACHE)/*

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/house/kasa.config $(DESTDIR)/etc/default/housekasa

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

