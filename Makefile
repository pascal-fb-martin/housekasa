
OBJS= housekasa_device.o housekasa.o
LIBOJS=

SHARE=/usr/local/share/house

all: housekasa kasacmd

clean:
	rm -f *.o *.a housekasa kasacmd

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housekasa: $(OBJS)
	gcc -g -O -o housekasa $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

kasacmd: kasacmd.c
	gcc -g -O -o kasacmd kasacmd.c

install:
	if [ -e /etc/init.d/housekasa ] ; then systemctl stop housekasa ; systemctl disable housekasa ; rm -f /etc/init.d/housekasa ; fi
	if [ -e /lib/systemd/system/housekasa.service ] ; then systemctl stop housekasa ; systemctl disable housekasa ; rm -f /lib/systemd/system/housekasa.service ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housekasa
	cp housekasa kasacmd /usr/local/bin
	chown root:root /usr/local/bin/housekasa /usr/local/bin/kasacmd
	chmod 755 /usr/local/bin/housekasa /usr/local/bin/kasacmd
	cp systemd.service /lib/systemd/system/housekasa.service
	chown root:root /lib/systemd/system/housekasa.service
	mkdir -p $(SHARE)/public/kasa
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/kasa
	cp public/* $(SHARE)/public/kasa
	chown root:root $(SHARE)/public/kasa/*
	chmod 644 $(SHARE)/public/kasa/*
	touch /etc/default/housekasa
	systemctl daemon-reload
	systemctl enable housekasa
	systemctl start housekasa

uninstall:
	systemctl stop housekasa
	systemctl disable housekasa
	rm -f /usr/local/bin/housekasa /usr/local/bin/kasacmd
	rm -f /lib/systemd/system/housekasa.service /etc/init.d/housekasa
	rm -rf $(SHARE)/public/kasa
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/house/kasa.config /etc/default/housekasa

