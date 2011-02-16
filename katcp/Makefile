include ../Makefile.inc

INC = -I.
LIB = libkatcp.a

SUB = examples utils
SRC = line.c netc.c dispatch.c loop.c log.c time.c shared.c misc.c server.c client.c ts.c nonsense.c notice.c job.c parse.c rpc.c queue.c map.c
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
	$(INSTALL) -d $(PREFIX)/include $(prefix)/lib
	$(INSTALL) $(LIB) $(PREFIX)/lib
	$(INSTALL) $(HDR) $(PREFIX)/include
