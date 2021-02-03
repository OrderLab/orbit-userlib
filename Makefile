.PHONY: all clean

PROGS_C = example.c
PROGS_CPP = micro.cpp
LIB = orbit.c

LINK.o = $(CXX) $(LDFLAGS) $(TARGET_ARCH)	# use c++ linker
CFLAGS = -O2
CXXFLAGS = -std=c++11 -O2
LDFLAGS = -static -pthread

binfiles := $(basename $(PROGS_C)) $(basename $(PROGS_CPP)) forktest

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

$(foreach prog,$(PROGS_C),$(eval $(call TEMPLATE_C,$(basename $(prog)))))
$(foreach prog,$(PROGS_CPP),$(eval $(call TEMPLATE_CPP,$(basename $(prog)))))

sinclude $(PROGS_C:%.c=%.d)
sinclude $(PROGS_CPP:%.cpp=%.d)
sinclude $(LIB:%.c=%.d)

clean:
	rm -f *.d *.o $(binfiles)
