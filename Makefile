.PHONY: all clean

PROGS_C = example.c
PROGS_CC = test/deadlock-detector-test.cc
PROGS_CPP = micro.cpp
LIB = orbit.c

LINK.o = $(CXX) $(LDFLAGS) $(TARGET_ARCH)	# use c++ linker
CFLAGS = -O2
CXXFLAGS = -std=c++11 -O2
LDFLAGS = -pthread
# Static linking with only -pthread will result in runtime segfault
# (ip=NULL). Add the following flags to LDFLAGS if needed:
# -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

C_INCLUDE_PATH = $(shell pwd)
CFLAGS += $(C_INCLUDE_PATH:%=-I%)

CPLUS_INCLUDE_PATH = $(shell pwd)
CXXFLAGS += $(CPLUS_INCLUDE_PATH:%=-I%)

binfiles := $(PROGS_C:%.c=%) $(PROGS_CC:%.cc=%) $(PROGS_CPP:%.cpp=%) forktest

all: $(binfiles)

define TEMPLATE_C
$(1).d: $(1).c
	$$(CC) $$(CFLAGS) $$^ -MM > $$@
$(1): $(addsuffix .o,$(1)) $(patsubst %.c,%.o,$(LIB))
endef

define TEMPLATE_CPP
$(1).d: $(1).cpp
	$$(CXX) $$(CXXFLAGS) $$^ -MM > $$@
$(1): $(addsuffix .o,$(1)) $(patsubst %.c,%.o,$(LIB))
endef

define TEMPLATE_CC
$(1).d: $(1).cc
	$$(CXX) $$(CXXFLAGS) $$^ -MM > $$@
$(1): $(addsuffix .o,$(1)) $(patsubst %.c,%.o,$(LIB))
endef

$(foreach prog,$(PROGS_C),$(eval $(call TEMPLATE_C,$(basename $(prog)))))
$(foreach prog,$(PROGS_CC),$(eval $(call TEMPLATE_CC,$(basename $(prog)))))
$(foreach prog,$(PROGS_CPP),$(eval $(call TEMPLATE_CPP,$(basename $(prog)))))

sinclude $(PROGS_C:%.c=%.d)
sinclude $(PROGS_CC:%.cc=%.d)
sinclude $(PROGS_CPP:%.cpp=%.d)
sinclude $(LIB:%.c=%.d)

clean:
	rm -f $(binfiles) $(binfiles:%=%.d) $(binfiles:%=%.d)
