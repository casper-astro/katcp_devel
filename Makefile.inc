# install target 
PREFIX=/usr/local

# where to find the KATCP library (change this in case katcp isn't included locally)
KATCP ?= katcp

# compiler flags 
CFLAGS += -Wall
#CFLAGS += -O2

# where the historic implementation diverges from the standard,
# force standard behaviour. Currently only one case: log-level affects
# all connections immediately in standard, otherwise local connection only
CFLAGS += -DKATCP_STRICT_CONFORMANCE

# how many messages to hold before forcing a flush, only useful 
# on memory constrained systems. When this value is unset the 
# system buffers as much as needed
CFLAGS += -DKATCP_FLUSH_THRESHOLD=4

# enable floating point support (floating point sensor type)
# unless you don't want to use your fpu (or don't have one) enable this
CFLAGS += -DKATCP_USE_FLOATS

# perform redundant checks, abort if things don't look alright
# enabling this reduces performance and makes the system less
# resistant to failures, but will also notice failures sooner
CFLAGS += -DKATCP_CONSISTENCY_CHECKS

# log certain noncritical failures to stderr 
CFLAGS += -DKATCP_STDERR_ERRORS
# amount of debugging logic to enable 
#CFLAGS += -DDEBUG=4
#CFLAGS += -DDEBUG
CFLAGS += -ggdb

GITVER = $(shell git describe  --tags --dirty  2> /dev/null || echo unknown)

CC = $(CROSS_COMPILE)gcc
LOCALCC = gcc

INSTALL = install
RM = rm -f
AR = ar
ECHO = echo

.PHONY: all clean install
