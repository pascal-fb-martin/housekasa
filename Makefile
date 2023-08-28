# HouseKasa - A simple home web server for control of TP-Link Kasa devices.
#
# Copyright 2023, Pascal Martin
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

HAPP=housekasa
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS= housekasa_device.o housekasa.o
LIBOJS=

all: housekasa kasacmd

clean:
	rm -f *.o *.a housekasa kasacmd

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housekasa: $(OBJS)
	gcc -Os -o housekasa $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lrt

kasacmd: kasacmd.c
	gcc -Os -o kasacmd kasacmd.c

# Distribution agnostic file installation -----------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housekasa
	cp housekasa kasacmd $(HROOT)/bin
	chown root:root $(HROOT)/bin/housekasa $(HROOT)/bin/kasacmd
	chmod 755 $(HROOT)/bin/housekasa $(HROOT)/bin/kasacmd
	mkdir -p $(SHARE)/public/kasa
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/kasa
	cp public/* $(SHARE)/public/kasa
	chown root:root $(SHARE)/public/kasa/*
	chmod 644 $(SHARE)/public/kasa/*
	touch /etc/default/housekasa

uninstall-app:
	rm -f $(HROOT)/bin/housekasa $(HROOT)/bin/kasacmd
	rm -rf $(SHARE)/public/kasa

purge-app:

purge-config:
	rm -rf /etc/house/kasa.config /etc/default/housekasa

# System installation. ------------------------------------------

include $(SHARE)/install.mak

