KATCP ?= ../katcp

include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp -ldl
CFLAGS += -fPIC
CFLAGS += -ggdb
CFLAGS += -DDEBUG

SERVER = tcpborphserver3
SRC = main.c raw.c loadbof.c tg.c tapper.c hwmon.c

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(SERVER)

#$(SERVER): $(OBJ) 
$(SERVER): $(OBJ) $(KATCP)/libkatcp.a
	$(CC) -o $@ $(OBJ) $(LIB)

clean: 
	$(RM) -f $(SERVER) *.o test-*

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

install: all
	$(INSTALL) $(SERVER) $(PREFIX)/sbin

test-bof: bof.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp

