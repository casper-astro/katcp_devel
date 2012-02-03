include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/scripts
VAR = $(PREFIX)/var/lib/kcs

SRC = functions.sh capture-start capture-stop capture-list capture-destination sync-now k7-accumulation-length k7-delay k7-snap-shot k7-gain k7-frequency-select halt restart xport restart-mode-ready restart-mode-nbc restart-mode-wbc restart-mode-wbc8k restart-mode-bwbc4a label-input enable-sensors corr

all:

clean:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
