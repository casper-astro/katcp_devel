KATCP ?= ../katcp

include ../Makefile.inc

CFLAGS := $(filter-out -DDEBUG,$(CFLAGS))

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = kcpfpg
SRC = fpg.c

OBJ = $(patsubst %.c,%.o,$(SRC))

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIB)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< $(INC)

clean:
	$(RM) $(OBJ) core $(EXE)

# Create symlink in /bin so that the upload_to_ram_and_program
# command in tcpborphserver3 will work.
install: all
	$(INSTALL) $(EXE) $(PREFIX)/bin
	ln -sf $(PREFIX)/bin/$(EXE) /bin/$(EXE)
