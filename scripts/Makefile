include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/
SRC = init.katcp functions.sh corr-init capture-start capture-stop capture-list k7-accumulation-length k7-delay k7-snap-shot k7-gain

all:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
