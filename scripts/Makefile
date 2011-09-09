include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/scripts
SRC = functions.sh capture-start capture-stop capture-list sync-now k7-accumulation-length k7-delay k7-snap-shot k7-gain k7-channel-select halt restart xport restart-mode-nbc restart-mode-wbc label-input enable-sensors corr

all:

clean:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
