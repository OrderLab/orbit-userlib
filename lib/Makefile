SRCDIR := src
OBJDIR := ../build/lib
BINDIR := $(OBJDIR)

sources	:= $(wildcard $(SRCDIR)/*.c)
include_dirs := include $(SRCDIR)

include ../defs.mk

# for building the shared library
CFLAGS += -fPIC
bins := $(BINDIR)/liborbit.a $(BINDIR)/liborbit.so

all: $(bins)

$(BINDIR)/liborbit.a: $(OBJDIR)/orbit.o | $(BINDIR)
	$(AR) -rc $@ $^

$(BINDIR)/liborbit.so: $(OBJDIR)/orbit.o | $(BINDIR)
	$(CC) -fPIC -shared $< -o $@

include ../rules.mk

.PHONY: all clean
