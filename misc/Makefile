include ../Makefile.inc

# in theory this should live in /etc/kcs
CMDSDIR = $(PREFIX)/lib/kcs

# startup routines
INITDIR = /etc/init.d

INITSRC = init-kcs
INITDST = kcs

CMDS    = init.katcp

all:

clean:

install: all
	$(INSTALL) -d $(CMDSDIR)
	$(INSTALL) $(CMDS) $(CMDSDIR)/$(CMDS)

	$(INSTALL) -d $(INITDIR)
	$(INSTALL) $(INITSRC) $(INITDIR)/$(INITDST)
