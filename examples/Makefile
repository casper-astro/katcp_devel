include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp
BUILD = unknown-0.1

EXE = client-example server-example 

all: $(EXE)

client-example: client-example.c
	$(CC) -DBUILD=\"$(BUILD)\" $(CFLAGS) $(INC) -o $@ $^ $(LIB)

server-example: server-example.c
	$(CC) -DBUILD=\"$(BUILD)\" $(CFLAGS) $(INC) -o $@ $^ $(LIB)

clean: 
	$(RM) $(EXE)

install: $(EXE)
	$(ECHO) "not installing examples"

