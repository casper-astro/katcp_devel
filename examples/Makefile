CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall
#CFLAGS += -DDEBUG -ggdb
INC = -I..
LIB = -L.. -lkatcp
RM = rm -f
BUILD = unknown-0.1

EXE = client-example server-example 

all: $(EXE)

client-example: client-example.c
	$(CC) -DBUILD=\"$(BUILD)\" $(CFLAGS) $(INC) -o $@ $^ $(LIB)

server-example: server-example.c
	$(CC) -DBUILD=\"$(BUILD)\" $(CFLAGS) $(INC) -o $@ $^ $(LIB)

clean: 
	$(RM) $(EXE)
