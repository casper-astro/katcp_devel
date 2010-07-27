prefix=/usr/local

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall
CFLAGS += -O2
CFLAGS += -DKATCP_USE_FLOATS
CFLAGS += -DPARANOID
#CFLAGS += -DDEBUG
#CFLAGS += -ggdb
AR = ar
RM = rm -f
INSTALL = install
INC = -I.

LIB = libkatcp.a

SUB = examples utils
SRC = line.c netc.c dispatch.c loop.c log.c time.c shared.c misc.c server.c client.c ts.c nonsense.c
HDR = katcp.h katcl.h katsensor.h katpriv.h
OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(LIB) 

$(LIB): $(OBJ)
	$(AR) rcs $(LIB) $(OBJ)

%.o: %.c *.h 
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

clean:
	$(RM) $(LIB) *.o core

install: all
	$(INSTALL) -d $(prefix)/include $(prefix)/lib
	$(INSTALL) $(LIB) $(prefix)/lib
	$(INSTALL) $(HDR) $(prefix)/include

