include ../common.mk

sources	:= orbit.c
objects	:= $(subst .c,.o,$(sources))
deps := $(subst .c,.d,$(sources))

all: liborbit.a liborbit.so
	@echo $(CFLAGS)

liborbit.a: $(objects)
	$(AR) $(ARFLAGS) $@ $^

clean:
	rm liborbit.a lib

.PHONY: all clean
