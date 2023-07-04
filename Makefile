
OBJS= housekasa_device.o housekasa.o
LIBOJS=

SHARE=/usr/local/share/house

# Local build ---------------------------------------------------

all: housekasa kasacmd

clean:
	rm -f *.o *.a housekasa kasacmd

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housekasa: $(OBJS)
	gcc -Os -o housekasa $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

kasacmd: kasacmd.c
	gcc -Os -o kasacmd kasacmd.c

# Distribution agnostic file installation -----------------------

install-files:
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housekasa
	cp housekasa kasacmd /usr/local/bin
	chown root:root /usr/local/bin/housekasa /usr/local/bin/kasacmd
	chmod 755 /usr/local/bin/housekasa /usr/local/bin/kasacmd
	mkdir -p $(SHARE)/public/kasa
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/kasa
	cp public/* $(SHARE)/public/kasa
	chown root:root $(SHARE)/public/kasa/*
	chmod 644 $(SHARE)/public/kasa/*
	touch /etc/default/housekasa

uninstall-files:
	rm -f /usr/local/bin/housekasa /usr/local/bin/kasacmd
	rm -rf $(SHARE)/public/kasa

purge-config:
	rm -rf /etc/house/kasa.config /etc/default/housekasa

# Distribution agnostic systemd support -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/housekasa.service
	chown root:root /lib/systemd/system/housekasa.service
	systemctl daemon-reload
	systemctl enable housekasa
	systemctl start housekasa

uninstall-systemd:
	if [ -e /etc/init.d/housekasa ] ; then systemctl stop housekasa ; systemctl disable housekasa ; rm -f /etc/init.d/housekasa ; fi
	if [ -e /lib/systemd/system/housekasa.service ] ; then systemctl stop housekasa ; systemctl disable housekasa ; rm -f /lib/systemd/system/housekasa.service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-config

# Void Linux install --------------------------------------------

install-void: install-files

uninstall-void: uninstall-files

purge-void: uninstall-void purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

