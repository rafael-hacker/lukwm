CC = cc
CFLAGS = -Wall -O2 -flto -s 
LIBS = -lX11
PREFIX ?= /usr/local

lukwm: lukwm.c config.h
	$(CC) $(CFLAGS) -o lukwm lukwm.c $(LIBS)

config.h:
	cp config.def.h config.h

install: lukwm
	install -Dm755 lukwm $(PREFIX)/bin/lukwm
	install -Dm644 lukwm.desktop /usr/share/xsessions/lukwm.desktop
	install -Dm644 lukwm.1 $(PREFIX)/share/man/man1/lukwm.1

uninstall:
	rm --force $(PREFIX)/bin/lukwm
	rm --force /usr/share/xsessions/lukwm.desktop
	rm --force $(PREFIX)/share/man/man1/lukwm.1

clean:
	rm --force lukwm

.PHONY: clean install uninstall
