include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/scripts
SRC = functions.sh corr-init capture-start capture-stop capture-list k7-accumulation-length k7-delay k7-snap-shot k7-gain halt restart

all:

clean:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
