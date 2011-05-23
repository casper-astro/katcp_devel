include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/scripts
SRC = functions.sh capture-start capture-stop capture-list k7-accumulation-length k7-delay k7-snap-shot k7-gain halt restart xport mode-wbc label-input enable-sensors wbc corr

all:

clean:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
