KATCP ?= ../katcp

include ../Makefile.inc

CFLAGS := $(filter-out -DDEBUG,$(CFLAGS))
CFLAGS += -DVERSION=\"$(GITVER)\"


INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = kcpcmd
SRC = cmd.c

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
