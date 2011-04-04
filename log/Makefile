include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = kl
SRC = kl.c

OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIB)

install: all
	$(INSTALL) $(EXE) $(PREFIX)/bin

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC)

clean:
	$(RM) $(OBJ) core $(EXE)
