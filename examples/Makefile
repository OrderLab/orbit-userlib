OBJDIR := ../build/examples
BINDIR := $(OBJDIR)
SRCDIR := .
LDFLAGS := -L../build/lib -l:liborbit.a -pthread

sources	:= $(wildcard $(SRCDIR)/*.c)
include_dirs := ../lib/include
bins := $(BINDIR)/example

include ../defs.mk

all: $(bins)

$(BINDIR)/example: $(OBJDIR)/example.o | $(BINDIR)
	$(CC) $^ -o $@ $(LDFLAGS)

include ../rules.mk

.PHONY: all clean
