KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP) -I../kcs
LIB = -L$(KATCP) -lkatcp
CFLAGS += -fPIC
CFLAGS += -DVERSION=\"$(GITVER)\"
LDFLAGS += -shared 
vpath %.c ../kcs

STATEMACHINE = statemachine.c
#MOD = mod_simple_ops.so
SRC = mod_conf_parser.c mod_simple_ops.c mod_roach_comms.c

OBJ = $(patsubst %.c,%.o,$(SRC)) $(patsubst %.c,%.o,$(STATEMACHINE))
MOD = $(patsubst %.c,%.so,$(SRC))

all: $(MOD)

%.so: %.o statemachine.o
	$(CC) $(LDFLAGS) -Wl,-soname=$@ $^ -o $@ $(LIB)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC) -o $@ 

clean:
	$(RM) $(OBJ) core $(MOD)

