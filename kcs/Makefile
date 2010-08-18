SERVER = kcs
SRC = kcserver.c basic.c

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


