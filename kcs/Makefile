include ../Makefile.inc

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

SERVER = kcs
SRC = shared.c
SRC += kurl.c roachpool.c fork-parent.c execpy.c parser.c kcserver.c basic.c

OBJ = $(patsubst %.c,%.o,$(SRC))
all: $(SERVER)

$(SERVER): $(OBJ) ../katcp/libkatcp.a
	$(CC) -o $@ $^ $(LIB)

clean: 
	$(RM) -f $(SERVER) *.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

install: all
	$(INSTALL) $(SERVER) $(PREFIX)/sbin

test-parser: parser.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp

test-execpy: execpy.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp -L../katcp -lkatcp

test-roachpool: roachpool.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp
  
test-kurl: kurl.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp
