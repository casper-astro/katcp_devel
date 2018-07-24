KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = dmon
SRC = dmon.c

# CFLAGS += -DSTANDALONE
# CFLAGS += -DDEBUG

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(EXE)

$(EXE): $(OBJ) $(KATCP)/libkatcp.a
	$(CC) -o $@ $^ $(LIB)

clean: 
	$(RM) -f $(EXE) *.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

install: all
	$(INSTALL) $(EXE) $(PREFIX)/sbin
