KATCP ?= ../katcp
#SSLBUILD = /home/adam/build/openssl-1.0.0e
include ../Makefile.inc

#INC = -I$(SSLBUILD)/include,$(KATCP)
#LIB = -L$(KATCP) -lkatcp
#LIB = -L$(SSLBUILD) -lssl -lcrypto -lz -ldl
LIB = -lssl 

EXE = wss
SRC = server.c wss.c
HDR = server.h

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
