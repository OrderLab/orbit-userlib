OBJDIR := ../build/test
BINDIR := $(OBJDIR)
SRCDIR := .
LDFLAGS := -L../build/lib -l:liborbit.a -pthread

sources	:= $(wildcard $(SRCDIR)/*.cpp $(SRCDIR)/*.cc)
include_dirs := ../lib/include
bins := $(BINDIR)/deadlock-detector-test $(BINDIR)/ttaslock

include ../defs.mk

all: $(bins)
	@echo $(objects)

$(BINDIR)/%: $(OBJDIR)/%.o | $(BINDIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

include ../rules.mk

.PHONY: all clean
