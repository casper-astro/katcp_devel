CFLAGS = -Wall
#CFLAGS += -O2
CFLAGS += -ggdb

# where to find the KATCP library (change this in case katcp isn't included locally)
KATCP = katcp

###############################################################################

SUB = katcp kcs examples

CC = $(CROSS_COMPILE)gcc
RM = rm -f

###############################################################################

all: all-dir
clean: clean-dir
install: install-dir

###############################################################################

# warning: below rewrites KATCP for subdirectory
all-dir clean-dir install-dir: 
	@for d in $(SUB); do if ! $(MAKE) -C $$d KATCP=../$(KATCP) $(subst -dir,,$@) ; then exit; fi; done
