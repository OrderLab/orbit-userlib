.PHONY: all clean

SRC = orbit.c example.c

LDFLAGS = -static -pthread

all: example

%.d: %.c
	$(CC) $(CFLAGS) $^ -MM > $@

sinclude $(SRC:%.c=%.d)

example: $(SRC:%.c=%.o)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.d *.o example
