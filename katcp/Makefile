include ../Makefile.inc

#.SUFFIXES:

VERSION = 0.1

BUILD = $(shell date -u +%Y-%m-%dT%H:%M:%S)
VERSION = $(GITVER)

INC = -I.
LIB = libkatcp.a
SHARED = libkatcp.so

#CFLAGS := $(filter-out -DDEBUG,$(CFLAGS))

CFLAGS += -fPIC 
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -DBUILD=\"$(BUILD)\"

SUB = examples utils
SRC = line.c netc.c dispatch.c loop.c log.c time.c shared.c misc.c server.c client.c ts.c nonsense.c notice.c job.c parse.c rpc.c queue.c map.c kurl.c version.c fork-parent.c avltree.c ktype.c stack.c services.c dbase.c arb.c spointer.c event.c bytebit.c endpoint.c generic-queue.c dpx-core.c dpx-forward.c dpx-katcp.c dpx-mgmt.c dpx-listen.c dpx-misc.c dpx-cmds.c dpx-vrbl.c dpx-info.c dpx-sensor.c
HDR = katcp.h katcl.h katpriv.h fork-parent.h avltree.h netc.h

OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(LIB)

both: $(LIB) $(SHARED) 

$(LIB): $(OBJ)
	$(AR) rcs $(LIB) $(OBJ)

$(SHARED): $(OBJ)
	$(CC) $(CFLAGS) -shared -Wl,-x,-soname=$(SHARED) -o $(SHARED) $(OBJ)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

show:
	@echo 'VERSION="$(VERSION)"'
	@echo 'BUILD="$(BUILD)"'

clean:
	$(RM) $(LIB) $(SHARED) *.o core

install: all
	$(INSTALL) -d $(PREFIX)/include $(PREFIX)/lib
	$(INSTALL) $(LIB) $(PREFIX)/lib
	$(INSTALL) $(HDR) $(PREFIX)/include
