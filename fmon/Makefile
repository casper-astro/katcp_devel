KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = kcpfmon
SRC = fmon.c

OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIB)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC)

clean:
	$(RM) $(OBJ) core $(EXE)

install: all
	$(INSTALL) $(EXE) $(PREFIX)/bin
