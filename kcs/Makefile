CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall
CFLAGS += -DDEBUG
CFLAGS += -ggdb

SERVER = kcs
SRC = execpy.c parser.c kcserver.c basic.c

OBJ = $(patsubst %.c,%.o,$(SRC))
INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

all: $(SERVER)

$(SERVER): $(OBJ)
	$(CC) -o $@ $^ $(LIB)

clean: 
	$(RM) -f $(SERVER) *.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INC)

test-parser: parser.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp

test-execpy: execpy.c 
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $^ -I../katcp -L../katcp -lkatcp
