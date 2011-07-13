KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP) -I../kcs
LIB = -L$(KATCP) -lkatcp
CFLAGS += -fPIC
CFLAGS += -DVERSION=\"$(GITVER)\"
LDFLAGS += -shared 
vpath %.c ../kcs

DEPENDS = statemachine.c actor.c
SRC = mod_media_man.c mod_conf_parser.c mod_simple_ops.c mod_roach_comms.c

DOBJ = $(patsubst %.c,%.o,$(DEPENDS))
OBJ  = $(patsubst %.c,%.o,$(SRC)) $(DOBJ)
MOD  = $(patsubst %.c,%.so,$(SRC))

all: $(MOD)

%.so: %.o $(DOBJ)
	$(CC) $(LDFLAGS) -Wl,-soname=$@ $^ -o $@ $(LIB)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC) -o $@ 

clean:
	$(RM) $(OBJ) core $(MOD)

