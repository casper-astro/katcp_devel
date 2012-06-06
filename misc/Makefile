include ../Makefile.inc

# in theory this should live in /etc/kcs
CMDSDIR = $(PREFIX)/lib/kcs
CMDS    = init.katcp

# startup routines
INITDIR = /etc/init.d
INITSCRIPT = kcs.initscript

# log rotation
LOGROTATEDIR = /etc/logrotate.d
LOGROTATE = kcs.logrotate


all:

clean:

install: all
	$(INSTALL) -d $(CMDSDIR)
	$(INSTALL) $(CMDS) $(CMDSDIR)/$(CMDS)

	$(INSTALL) -d $(INITDIR)
	$(INSTALL) $(INITSCRIPT) $(INITDIR)/$(basename $(INITSCRIPT))

	$(INSTALL) -d $(LOGROTATEDIR)
	$(INSTALL) $(LOGROTATE) $(LOGROTATEDIR)/$(basename $(LOGROTATE))
