include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/
SRC = init.katcp functions.sh corr-init capture-start capture-stop k7-accumulation-length k7-delay

all:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
