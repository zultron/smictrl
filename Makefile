
CFLAGS = -O2 -Wall
PCILIB = /usr/lib/libpci.a

smictrl: smictrl.o $(PCILIB) -lz

clean:
	rm -f smictrl smictrl.o
