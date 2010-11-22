include Makefile.inc 

###############################################################################

SUB = katcp cmd kcs examples

###############################################################################

all: all-dir
clean: clean-dir
install: install-dir

###############################################################################

# warning: below rewrites KATCP for subdirectory
all-dir clean-dir install-dir: 
	@for d in $(SUB); do if ! $(MAKE) -C $$d KATCP=../$(KATCP) $(subst -dir,,$@) ; then exit; fi; done
