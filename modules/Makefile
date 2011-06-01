KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP) -I../kcs
LIB = -L$(KATCP) -lkatcp
CFLAGS += -fPIC
CFLAGS += -DVERSION=\"$(GITVER)\"
LDFLAGS += -shared 

MOD = mod_simple_ops.so
SRC = mod_simple_ops.c ../kcs/statemachine.c

OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(MOD)

$(MOD): $(OBJ)
	$(CC) $(LDFLAGS) -Wl,-soname=$(MOD) -o $@ $(STATEMACHINE) $(OBJ) $(LIB)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC) -o $@ 

clean:
	$(RM) $(OBJ) core $(MOD)

