OBJDIR := ../build/benchmark
BINDIR := $(OBJDIR)
SRCDIR := .
LDFLAGS := -pthread

sources	:= $(wildcard $(SRCDIR)/*.cpp)
include_dirs := ../lib/include
bins := $(BINDIR)/forktest $(BINDIR)/micro

include ../defs.mk

all: $(bins)

$(BINDIR)/forktest: $(OBJDIR)/forktest.o | $(BINDIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BINDIR)/micro: $(OBJDIR)/micro.o | $(BINDIR)
	$(CXX) $^ -o $@ $(LDFLAGS) -L../build/lib -l:liborbit.a 

include ../rules.mk

.PHONY: all clean
