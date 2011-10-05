# install target 
PREFIX=/usr/local

# where to find the KATCP library (change this in case katcp isn't included locally)
KATCP ?= katcp

# compiler flags 
CFLAGS += -Wall
CFLAGS += -O2
CFLAGS += -DKATCP_USE_FLOATS
CFLAGS += -DPARANOID
#CFLAGS += -DDEBUG=4
#CFLAGS += -DDEBUG
CFLAGS += -DREPORT
CFLAGS += -ggdb

GITVER = $(shell git describe  --tags --dirty  2> /dev/null || echo unknown)

CC = $(CROSS_COMPILE)gcc
LOCALCC = gcc

INSTALL = install
RM = rm -f
AR = ar
ECHO = echo

.PHONY: all clean install
