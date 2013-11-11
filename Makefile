include Makefile.inc 

###############################################################################

LIBRARY = katcp
APPS = kcs cmd examples sq bulkread tmon log fmon modules tcpborphserver3 msg delay par sgw xport con dmon 
MISC = scripts misc 

EVERYTHING = $(LIBRARY) $(APPS) $(MISC)

###############################################################################

all: $(patsubst %,%-all,$(EVERYTHING))
clean: $(patsubst %,%-clean,$(EVERYTHING))
install: $(patsubst %,%-install,$(EVERYTHING))

$(patsubst %,%-all,$(APPS)): $(patsubst %,%-all,$(LIBRARY))

%-all %-clean %-install:
	$(MAKE) -C $(shell echo $@ | cut -f1 -d- ) KATCP=../$(KATCP) $(shell echo $@ | cut -f2 -d-)

###############################################################################
# old style build, can not be run in parallel 
#
# all: all-dir
# clean: clean-dir
# install: install-dir
#
# warning: below rewrites KATCP for subdirectory
# all-dir clean-dir install-dir: 
#	@for d in $(SUB); do if ! $(MAKE) -C $$d KATCP=../$(KATCP) $(subst -dir,,$@) ; then exit; fi; done
