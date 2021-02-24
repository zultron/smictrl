KSRC = /lib/modules/$(shell uname -r)/source

CFLAGS = -O2 -Wall -I $(KSRC)/include
LDFLAGS = -lz -lpci

smictrl: smictrl.c
	$(CC) $(CFLAGS) smictrl.c $(LDFLAGS) -o smictrl

clean:
	rm -f smictrl
