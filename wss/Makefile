KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP)
#LIB = -L$(KATCP) -lkatcp
LIB = -lssl

EXE = wss
SRC = server.c wss.c

# CFLAGS += -DSTANDALONE
# CFLAGS += -DDEBUG

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(EXE)

#$(EXE): $(OBJ) $(KATCP)/libkatcp.a
$(EXE): $(OBJ)
	$(CC) -o $@ $^ $(LIB)

clean: 
	$(RM) -f $(EXE) *.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

install: all
	$(INSTALL) $(EXE) $(PREFIX)/sbin
