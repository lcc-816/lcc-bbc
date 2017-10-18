TARGET=6502
OS=bbc
CC=cc
CFLAGS=-g
LDFLAGS=-g
INCLUDES=-I.. -I../../c

D=$(TARGET)-$(OS)

all:	$(D)/rcc

OBJS=$(D)/gen.o

$(D)/gen.o:	gen.c reginfo.h
		cd $(D); $(CC) -c $(CFLAGS) $(INCLUDES) ../gen.c

include ../c/makefile

$(COBJS) $(OBJS):	config.h

RUN=run
include ../tst/makefile
