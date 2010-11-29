RM = rm -f
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall
#CFLAGS += -DDEBUG
CFLAGS += -ggdb
LDFLAGS = 

INC = -I$(KATCP)
LIB = -L$(KATCP) -lkatcp

EXE = kcpsq
SRC = sq.c

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
